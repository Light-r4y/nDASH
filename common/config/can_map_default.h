#pragma once

// The CAN table used until a saved one exists (and after "reset to built-in"): the
// rusEFI broadcast frames from base ID 0x200, all fields Intel (@1), 11-bit IDs.
// Source: the rusEFI DBC. "boost" is the field of "map" again (the display subtracts the atmosphere).
// Pressures are kPa (raw * 0.0333), temperatures deg C, vbat V,
// afr = lambda * 14.7 (gasoline; for another fuel change the factor of "afr").
#define LND_CAN_MAP_DEFAULT_JSON \
    "{\"version\":1,\"entries\":[{\"signal\":\"rpm\",\"id\":\"0x201\",\"ext\":false,\"start\":0,\"len\":16,\"order\":\"intel\"," \
    "\"signed\":false,\"factor\":1,\"offset\":0,\"timeout\":2000},{\"signal\":\"speed\",\"id\":\"0x201\",\"ext\":false,\"sta" \
    "rt\":48,\"len\":8,\"order\":\"intel\",\"signed\":false,\"factor\":1,\"offset\":0,\"timeout\":2000},{\"signal\":\"map\"," \
    "\"id\":\"0x203\",\"ext\":false,\"start\":0,\"len\":16,\"order\":\"intel\",\"signed\":false,\"factor\":0.0333,\"offset\":" \
    "0,\"timeout\":2000},{\"signal\":\"boost\",\"id\":\"0x203\",\"ext\":false,\"start\":0,\"len\":16,\"order\":\"intel\",\"sig" \
    "ned\":false,\"factor\":0.0333,\"offset\":0,\"timeout\":2000},{\"signal\":\"coolant\",\"id\":\"0x203\",\"ext\":false,\"" \
    "start\":16,\"len\":8,\"order\":\"intel\",\"signed\":false,\"factor\":1,\"offset\":-40,\"timeout\":2000},{\"signal\":\"" \
    "iat\",\"id\":\"0x203\",\"ext\":false,\"start\":24,\"len\":8,\"order\":\"intel\",\"signed\":false,\"factor\":1,\"offset\":" \
    "-40,\"timeout\":2000},{\"signal\":\"fuel_level\",\"id\":\"0x203\",\"ext\":false,\"start\":56,\"len\":8,\"order\":\"inte" \
    "l\",\"signed\":false,\"factor\":0.5,\"offset\":0,\"timeout\":2000},{\"signal\":\"oil_press\",\"id\":\"0x204\",\"ext\":f" \
    "alse,\"start\":16,\"len\":16,\"order\":\"intel\",\"signed\":false,\"factor\":0.0333,\"offset\":0,\"timeout\":2000},{" \
    "\"signal\":\"oil_temp\",\"id\":\"0x204\",\"ext\":false,\"start\":32,\"len\":8,\"order\":\"intel\",\"signed\":false,\"fact" \
    "or\":1,\"offset\":-40,\"timeout\":2000},{\"signal\":\"vbat\",\"id\":\"0x204\",\"ext\":false,\"start\":48,\"len\":16,\"or" \
    "der\":\"intel\",\"signed\":false,\"factor\":0.001,\"offset\":0,\"timeout\":2000},{\"signal\":\"afr\",\"id\":\"0x207\",\"" \
    "ext\":false,\"start\":0,\"len\":16,\"order\":\"intel\",\"signed\":false,\"factor\":0.00147,\"offset\":0,\"timeout\":2" \
    "000},{\"signal\":\"fuel_press\",\"id\":\"0x207\",\"ext\":false,\"start\":32,\"len\":16,\"order\":\"intel\",\"signed\":fa" \
    "lse,\"factor\":0.0333,\"offset\":0,\"timeout\":2000}]}"
