#pragma once

// We have a bit of a mess with too many useful usages of CAN and too little documentation at https://github.com/rusefi/rusefi/wiki/CAN

#define LND_BENCH_TEST_BASE_ADDRESS 0x770000

// rusEFI bench-test / dash packet IDs (extended frames)
enum {
  LND_BENCH_EVENT_COUNTERS = LND_BENCH_TEST_BASE_ADDRESS, // ECU output
  LND_BENCH_RAW_ANALOG_1,                                 // ECU output
  LND_BENCH_HW_QC_IO_CONTROL,                             // ECU input special bench quality control mode, not to be confused with ECU_CAN_BUS_USER_CONTROL
  LND_BENCH_BOARD_STATUS,                                 // ECU output
  LND_BENCH_BUTTON_COUNTERS,                              // ECU output 0x770004
  LND_BENCH_IO_META_INFO,                                 // ECU output
  LND_BENCH_RAW_ANALOG_2,                                 // ECU output
  LND_BENCH_PIN_STATE,                                    // ECU output 0x770007
  LND_BENCH_AUX_DIGITAL_COUNTERS,                         // ECU output
  LND_BENCH_MANUAL_PIN_TEST,                              // ECU output
  LND_BENCH_FW_WIPE_OUT,                                  // OpenBLT input 0x77000A
  LND_BENCH_HW_QC_TESTER_PRESENT,                         // ECU input 0x77000B
  LND_BENCH_ECU_CAN_BUS_USER_CONTROL,                     // ECU input 0x77000C - user actions to control ECU
  LND_BENCH_ECU_CONFIG_BROADCAST,                         // ECU output 0x77000D
  LND_BENCH_ECU_CAN_BUS_SETTINGS_CONTROL,                 // ECU input 0x77000E - user actions to change ECU settings
  LND_BENCH_DASH_ALIVE,                                   // ECU input 0x77000F - dash alive status
  LND_BENCH_ECU_GET_CALIBRATION,                          // ECU output 0x770010
  LND_BENCH_ECU_SET_CALIBRATION,                          // ECU input 0x770011
  LND_BENCH_ECU_REQ_CALIBRATION,                          // ECU input 0x770012
};

#define LND_BENCH_HEADER 0x66

// bench_test_io_control
enum {
  LND_CAN_BENCH_GET_COUNT,
  LND_CAN_QC_OUTPUT_CONTROL_SET,
  LND_CAN_QC_OUTPUT_CONTROL_CLEAR,
  LND_CAN_BENCH_SET_ENGINE_TYPE,
  LND_CAN_BENCH_EXECUTE_BENCH_TEST,
  LND_CAN_BENCH_QUERY_PIN_STATE,
  LND_CAN_BENCH_START_PIN_TEST,
  LND_CAN_BENCH_END_PIN_TEST,
  LND_CAN_QC_ETB,
};
