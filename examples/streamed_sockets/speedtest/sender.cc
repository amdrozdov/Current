#include <deque>

#include "../../../blocks/xterm/progress.h"
#include "../../../blocks/xterm/vt100.h"
#include "../../../bricks/dflags/dflags.h"
#include "../../../bricks/net/tcp/tcp.h"
#include "../../../bricks/strings/printf.h"
#include "../../../bricks/time/chrono.h"

DEFINE_string(host, "127.0.0.1", "The destination address to send data to.");
DEFINE_uint16(port, 9001, "The destination port to send data to.");
DEFINE_double(buffer_size_gb, 2.0, "Write buffer size.");
DEFINE_double(window_size_seconds, 5.0, "The length of sliding window the throughput within which is reported.");
DEFINE_double(window_size_gb, 20.0, "The maximum amount of data per the sliding window to report the throughput.");
DEFINE_double(output_frequency, 0.1, "The minimim amount of time, in seconds, between terminal updates.");
DEFINE_double(max_seconds_of_no_sends, 2.5, "Terminate the connection if can't send anything for this long.");

int main(int argc, char** argv) {
  ParseDFlags(&argc, &argv);

  std::chrono::microseconds const t_window_size(static_cast<int64_t>(FLAGS_window_size_seconds * 1e6));
  size_t const window_size_bytes = static_cast<size_t>(FLAGS_window_size_gb * 1e9);
  std::chrono::microseconds const t_output_frequency(static_cast<int64_t>(FLAGS_output_frequency * 1e6));

  using namespace current::net;
  using namespace current::vt100;

  current::ProgressLine progress;

  progress << current::strings::Printf("allocating %.1lfGB", FLAGS_buffer_size_gb);
  size_t const buffer_size = static_cast<size_t>(1e9 * FLAGS_buffer_size_gb);
  std::vector<char> data(buffer_size);

  progress << current::strings::Printf("initializing %.1lfGB", FLAGS_buffer_size_gb);
  memset(&data[0], '.', buffer_size);
  {
    // A poor man's way of creating a somewhat random buffer by not spending much time doing it. -- D.K.
    uint8_t tmp = 0;
    for (size_t i = 0; i < buffer_size; i += (rand() & 0xff)) {
      data[i] = 'a' + tmp;
      tmp = (tmp + 1) % 26;
    }
  }

  progress << "preparing to send";
  while (true) {
    try {
      Connection connection(ClientSocket(FLAGS_host, FLAGS_port));
      progress << "connected, " << magenta << connection.LocalIPAndPort().ip << ':' << connection.LocalIPAndPort().port
               << reset << " => " << cyan << connection.RemoteIPAndPort().ip << ':' << connection.RemoteIPAndPort().port
               << reset;
      size_t total_bytes_sent = 0ull;
      std::deque<std::pair<std::chrono::microseconds, size_t>> history;  // { unix epoch time, total bytes received }.
      std::chrono::microseconds t_next_output = current::time::Now() + t_output_frequency;
      std::chrono::microseconds t_last_successful_receive = current::time::Now();
      while (true) {
        connection.BlockingWrite(reinterpret_cast<void const*>(&data[0]), data.size(), true);
        std::chrono::microseconds const t_now = current::time::Now();
        total_bytes_sent += data.size();
        t_last_successful_receive = t_now;
        history.emplace_back(t_now, total_bytes_sent);
        std::chrono::microseconds const t_cutoff = t_now - t_window_size;
        size_t const size_cutoff = (total_bytes_sent > window_size_bytes ? total_bytes_sent - window_size_bytes : 0);
        if (t_now >= t_next_output) {
          if (history.size() >= 2) {
            while (history.size() > 2 && (history.front().first <= t_cutoff || history.front().second <= size_cutoff)) {
              history.pop_front();
            }
            double const gb = 1e-9 * (history.back().second - history.front().second);
            double const s = 1e-6 * (history.back().first - history.front().first).count();
            progress << bold << green << current::strings::Printf("%.2lfGB/s", gb / s) << reset << ", " << bold
                     << yellow << current::strings::Printf("%.2lfGB", gb) << reset << '/' << bold << blue
                     << current::strings::Printf("%.1lfs", s) << reset << ", " << magenta
                     << connection.LocalIPAndPort().ip << ':' << connection.LocalIPAndPort().port << reset << " => "
                     << cyan << connection.RemoteIPAndPort().ip << ':' << connection.RemoteIPAndPort().port << reset;
          }
          t_next_output = t_now + t_output_frequency;
        }
      }
    } catch (current::net::SocketConnectException const&) {
      progress << "can not connect to " << red << bold << FLAGS_host << ':' << FLAGS_port << reset;
    } catch (current::Exception const& e) {
      progress << red << bold << "error" << reset << ": " << e.OriginalDescription() << reset;
    }
  }
}
