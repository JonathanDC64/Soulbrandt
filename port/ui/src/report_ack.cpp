// Written for Demon's Souls. See report_ack.hpp.
#include "report_ack.hpp"

#include "ack.hpp"
#include "report.hpp"

#include <string>
#include <utility>

namespace ui {

void with_zip_ack(std::function<void()> then) {
    const std::string where =
        report::no_desktop()
            ? "The report is saved with a link to the issue page. On a PC, drag the .zip into the issue's \"Report "
              "file\" box."
            : "Drag the .zip from the folder that opens into the issue's \"Report file\" box.";
    with_ack(
        {
            .file = L"report_zip_ack.txt",
            .kept = "The player has been told to attach the report's zip to the issue (report_ack.cpp).\n",
            .title = "Attach the report file?",
            .bodyText = where + " Without it, a report can rarely be solved.",
            .tick = "I'll attach the report file to my issue",
        },
        std::move(then));
}

}  // namespace ui
