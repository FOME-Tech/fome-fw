mkdir coverage

lcov -c --directory . --output-file ./coverage/main_coverage.info
genhtml ./coverage/main_coverage.info --output-directory ./coverage/html_report
