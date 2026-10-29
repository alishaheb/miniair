# Runs the engine executable on the synthetic scenario (stdin -> stdout) and
# checks the exact alert sequence documented in
# ingestion/miniair_ingest/synthetic.py.
#
# Usage: cmake -DENGINE=<path> -DINPUT=<file.jsonl> -P cli_test.cmake

execute_process(
  COMMAND ${ENGINE} --input stdin
  INPUT_FILE ${INPUT}
  OUTPUT_VARIABLE output
  ERROR_VARIABLE log
  RESULT_VARIABLE exit_code)

if(NOT exit_code EQUAL 0)
  message(FATAL_ERROR "engine exited with ${exit_code}\n${log}")
endif()

# Keys are written in alphabetical order, so these fields are adjacent.
set(expected
  "\"cycle_time\":1700000005.0,\"event\":\"NEW\",\"icao24_a\":\"4d0301\",\"icao24_b\":\"4d0302\",\"severity\":\"PREDICTED\""
  "\"cycle_time\":1700000070.0,\"event\":\"NEW\",\"icao24_a\":\"4d0101\",\"icao24_b\":\"4d0102\",\"severity\":\"PREDICTED\""
  "\"cycle_time\":1700000125.0,\"event\":\"UPDATED\",\"icao24_a\":\"4d0301\",\"icao24_b\":\"4d0302\",\"severity\":\"LOSS_OF_SEPARATION\""
  "\"cycle_time\":1700000140.0,\"event\":\"CLEARED\",\"icao24_a\":\"4d0301\",\"icao24_b\":\"4d0302\""
  "\"cycle_time\":1700000190.0,\"event\":\"UPDATED\",\"icao24_a\":\"4d0101\",\"icao24_b\":\"4d0102\",\"severity\":\"LOSS_OF_SEPARATION\""
  "\"cycle_time\":1700000255.0,\"event\":\"CLEARED\",\"icao24_a\":\"4d0101\",\"icao24_b\":\"4d0102\"")

string(REGEX MATCHALL "[^\n]+" lines "${output}")
list(LENGTH lines line_count)
list(LENGTH expected expected_count)
if(NOT line_count EQUAL expected_count)
  message(FATAL_ERROR "expected ${expected_count} alert events, got ${line_count}:\n${output}")
endif()

foreach(item IN LISTS expected)
  string(FIND "${output}" "${item}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "missing alert event:\n  ${item}\nfull output:\n${output}")
  endif()
endforeach()

# The pair 2,000 ft apart must never raise an alert.
string(FIND "${output}" "4d0201" position)
if(NOT position EQUAL -1)
  message(FATAL_ERROR "false alert for the vertically separated pair:\n${output}")
endif()

message(STATUS "engine produced the expected ${line_count} alert events")
