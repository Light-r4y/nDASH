# Trying the web interface without hardware

`mock_server.py` serves the pages from `www/` in an ordinary browser on a computer - no ESP32, no
firmware build, no CAN bus.

```
python tools/webtest/mock_server.py            # http://localhost:8088/
python tools/webtest/mock_server.py 8090       # another port
python tools/webtest/mock_server.py --password test1234   # as if a settings password were set
```

Pages: `/` (status, brightness), `/layout.html` (screens), `/can.html` (settings, table, sniffer),
`/access.html` (password, Wi-Fi), `/ota` (update).

## What it does

- Serves `www/` as it is, so edits show up after a refresh (caching is off).
- Takes `/ota` from `tasks/webcfg/ota_page.h`, the built-in layout from
  `common/config/layout_default.h` and the CAN preset from `common/config/can_map_default.h` -
  the same files that are compiled into the firmware.
- Replaces the `/api/*` endpoints of `tasks/webcfg/webcfg_task.c`: `status`, `auth/*`, `layout`,
  `display`, `wifi`, `can`, `can/map`, `can/frames`. State lives in the memory of the process.
- Checks what the firmware checks: a layout needs at least one screen; a CAN table is rejected as a whole
  if an entry has a bad ID, bit field or a repeated signal; the Wi-Fi can only be changed with a settings
  password set; with `--password` modifying requests need it (Basic Auth, the login is ignored).
- The sniffer imitates the board: the first reply shows only the IDs of the CAN table, IDs outside it
  appear while the page keeps polling (the ISR filter lifts for 3 seconds after each request).
- `/ota` upload is only simulated: nothing is written. `?fail=1` - a write error, `?hang=1` - a dropped
  connection, `?stall=1` - a reply that never comes, `?same=1` - the same image as installed.

## What it does not do

It does not check the screen itself (how the layout looks on the display) and does not run the C code;
the validation is a Python copy of it, so after changing `layout.c` or `can_map.c` keep `mock_server.py`
in step.
