/*******************************************************************************
The MIT License (MIT)

Copyright (c) 2016 Dmitry "Dima" Korolev <dmitry.korolev@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*******************************************************************************/

#ifndef KARL_RENDER_H
#define KARL_RENDER_H

#include "schema_karl.h"

#include "../Bricks/dot/graphviz.h"

namespace current {
namespace karl {

const std::string h1_begin = "<FONT POINT-SIZE='24' FACE='Courier'><B>";
const std::string h1_end = "</B></FONT>";
const std::string small_link_begin = "<FONT POINT-SIZE='13' FACE='Courier' COLOR='blue'><B><U>";
const std::string small_link_end = "</U></B></FONT>";
const std::string medium_text_begin = "<FONT POINT-SIZE='11' FACE='Courier'>";
const std::string medium_text_end = "</FONT>";
const std::string medium_link_begin = "<FONT POINT-SIZE='11' FACE='Courier' COLOR='black'><B><U>";
const std::string medium_link_end = "</U></B></FONT>";
const std::string tiny_text_begin = "<FONT POINT-SIZE='8' FACE='Courier'>";
const std::string tiny_text_end = "</FONT>";
const std::string width_marker = "<BR/>" + h1_begin + std::string(8u, ' ') + h1_end;

struct GenericRenderer {
  std::ostream& os;
  const std::chrono::microseconds now;
  GenericRenderer(std::ostream& os, std::chrono::microseconds now) : os(os), now(now) {}

  template <typename T>
  void operator()(const T& user_status) {
    user_status.Render(os, now);
  }
};

// Render Karl's status page as a GraphViz directed graph.
template <typename INNER_STATUSES_VARIANT>
inline graphviz::DiGraph Render(const current::karl::GenericKarlStatus<INNER_STATUSES_VARIANT>& status,
                                const std::string& title = "Graph",
                                const std::string& github_repo_url = "") {
  using namespace graphviz;

  std::chrono::microseconds now = status.now;

  graphviz::DiGraph graph;

  graph.Title(title);

  graph["label"] = current::strings::Printf("Generated %s, from `%s` to `%s`, in %.1lf seconds.",
                                            current::FormatDateTime(now).c_str(),
                                            strings::TimeDifferenceAsHumanReadableString(status.from - now).c_str(),
                                            strings::TimeDifferenceAsHumanReadableString(status.to - now).c_str(),
                                            1e-6 * status.generation_time.count());
  graph["labelloc"] = "b";
  graph["fontname"] = "Courier";
  graph["fontsize"] = "24";

  std::unordered_map<std::string, Node> services;                      // Codename -> `Node`, to add groups and edges.
  std::unordered_map<std::string, std::vector<std::string>> machines;  // IP -> [ Codename ], to manage groups.

  // Layout right to left. It's same as left to right, but as our edges are "follower -> master",
  // it makes sense to have the arrows point right to left.
  graph.RankDir() = "RL";

  // Add all services to the graph.
  for (const auto& machine : status.machines) {
    const std::string& ip = machine.first;
    for (const auto& e : machine.second.services) {
      const std::string& codename = e.first;
      const auto& service = e.second;
      std::ostringstream os;
      os << "<TABLE CELLBORDER='0' CELLSPACING='5'>";

      // Top row: Service name, no link.
      os << "<TR><TD COLSPAN='2' ALIGN='center'>" << tiny_text_begin + "service" + tiny_text_end + "<BR/>" << h1_begin
         << service.service << h1_end << "</TD></TR>";

      // First section, codename and up/down status.
      {
        struct up_down_renderer {
          std::chrono::microseconds now;
          const std::string& codename;
          up_down_renderer(std::chrono::microseconds now, const std::string& codename) : now(now), codename(codename) {}
          std::vector<std::string> cells;
          void operator()(const current_service_state::up& up) {
            cells.push_back("<TD>" + medium_text_begin + "up " +
                            strings::TimeIntervalAsHumanReadableString(now - up.start_time_epoch_microseconds) +
                            medium_text_end + "</TD>");
            cells.push_back("<TD HREF='./snapshot/" + codename + "?nobuild'>" + medium_link_begin + "updated " +
                            up.last_keepalive_received + medium_link_end + "</TD>");
          }
          void operator()(const current_service_state::down& down) {
            cells.push_back("<TD>" + medium_text_begin + "started " +
                            strings::TimeDifferenceAsHumanReadableString(down.start_time_epoch_microseconds - now) +
                            medium_text_end + "</TD>");
            cells.push_back("<TD HREF='./snapshot/" + codename + "?nobuild'>" + medium_link_begin + "down, last seen " +
                            down.last_keepalive_received + medium_link_end + "</TD>");
          }
        };
        up_down_renderer up_down(now, codename);
        service.currently.Call(up_down);

        // const auto url = service.url_status_page_direct;
        const auto url = "./live/" + codename;
        const auto body = tiny_text_begin + "codename" + tiny_text_end + "<BR/>" + small_link_begin + codename +
                          small_link_end + width_marker;

        os << "<TR><TD ROWSPAN='" << up_down.cells.size() + 1 << "' HREF='" << url << "'>" << body << "</TD>";
        os << up_down.cells[0] << "</TR>";
        for (size_t i = 1; i < up_down.cells.size(); ++i) {
          os << "<TR>" << up_down.cells[i] << "</TR>";
        }
        os << "<TR><TD><BR/></TD></TR>";
      }

      // Second section, build info.
      {
        const std::string commit_text = Exists(service.git_commit) ? Value(service.git_commit).substr(0, 6) : "build";

        const auto build_url = "./build/" + codename;
        const auto build_body = tiny_text_begin + "commit" + tiny_text_end + "<BR/>" + small_link_begin + commit_text +
                                small_link_end + width_marker;

        std::vector<std::string> cells;
        {
          if (service.build_time_epoch_microseconds.count()) {
            // `Build of YYYY/MM/DD HH:MM:SS` cell.
            const auto build_date_text =
                std::string("build of ") + current::FormatDateTime(service.build_time_epoch_microseconds);
            const auto build_date_body = medium_text_begin + build_date_text + medium_text_end;
            cells.push_back("<TD>" + build_date_body + "</TD>");

            // `built X days ago` cell with the link to a particular commit.
            const auto built_ago_text =
                "built " + strings::TimeDifferenceAsHumanReadableString(service.build_time_epoch_microseconds - now);
            const auto built_ago_body = medium_link_begin + built_ago_text + medium_link_end;
            if (!github_repo_url.empty() && Exists(service.git_commit)) {
              const auto github_url = github_repo_url + "/commit/" + Value(service.git_commit);
              cells.push_back("<TD HREF='" + github_url + "'>" + built_ago_body + "</TD>");
            } else {
              cells.push_back("<TD>" + built_ago_body + "</TD>");
            }
          }
          // `{branch_name}, {dirty|clean}` cell with the link to a github branch.
          if (Exists(service.git_branch) && Exists(service.git_dirty)) {
            const auto& git_branch = Value(service.git_branch);
            const bool git_dirty = Value(service.git_dirty);
            const auto git_branch_text = git_branch + ", " + (git_dirty ? "dirty" : "clean");
            const auto git_branch_body = medium_link_begin + git_branch_text + medium_link_end;
            if (!github_repo_url.empty()) {
              const auto github_url = github_repo_url + "/tree/" + git_branch;
              cells.push_back("<TD HREF='" + github_url + "'>" + git_branch_body + "</TD>");
            } else {
              cells.push_back("<TD>" + git_branch_body + "</TD>");
            }
          }
        }

        if (!cells.empty()) {
          os << "<TR><TD ROWSPAN='" << cells.size() + 1 << "' HREF='" << build_url << "'>" << build_body << "</TD>";
          os << cells[0] << "</TR>";
          for (size_t i = 1; i < cells.size(); ++i) {
            os << "<TR>" << cells[i] << "</TR>";
          }
        }
        os << "<TR><TD><BR/></TD></TR>";
      }

      // Final section, user report.
      if (Exists(service.runtime)) {
        Value(service.runtime).Call(GenericRenderer(os, now));
      }
      os << "</TABLE>";

      graph += (services[service.codename] = Node(os.str()).HTML().Shape("none"));
      machines[ip].push_back(service.codename);
    }
  }

  // Render service dependencies.
  for (const auto& machine : status.machines) {
    for (const auto& service : machine.second.services) {
      const Node& from = services[service.first];
      for (const auto& into : service.second.dependencies) {
        graph += Edge(from, services[into]);
      }
    }
  }

  // Group services by machines.
  for (const auto& machine : status.machines) {
    const auto& m = machine.second;
    const bool is_localhost = (machine.first == "127.0.0.1");
    auto group = Group()
                     .Label((is_localhost ? "localhost" : machine.first) + '\n' +
                            (Exists(m.cloud_instance_name) ? Value(m.cloud_instance_name) + '\n' : "") +
                            (Exists(m.cloud_availability_group) ? Value(m.cloud_availability_group) + '\n' : "") +
                            (is_localhost ? "" : machine.second.time_skew))
                     .LabelLoc("t")
                     .FontName("Courier")
                     .FontSize("32")
                     .GraphStyle("dashed");
    for (const auto& codename : machines[machine.first]) {
      group.Add(services[codename]);
    }
    graph.Add(group);
  }

  return graph;
}

}  // namespace current::karl
}  // namespace current

#endif  // KARL_RENDER_H
