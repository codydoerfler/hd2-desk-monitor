// Host-side stub for ArduinoJson, for the preview build only.
//
// src/hd2_api.h includes ArduinoJson and declares getJson(..., JsonDocument&)
// but the preview never calls the API -- it feeds the renderer hand-built
// models. Declaring the type is enough for that header to parse; the HD2Api
// methods are simply never linked, since render_preview.cpp does not
// reference them.
#pragma once

class JsonDocument {};
