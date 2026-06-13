// =============================================================================
// json_dom.h — In-memory JSON DOM used by the test harness
// =============================================================================
// The disting NT serialisation API (`_NT_jsonStream` / `_NT_jsonParse`) is
// host-side opaque. For host tests we substitute an in-memory document model
// so plugin `serialise()` / `deserialise()` callbacks can be exercised in a
// round-trip without touching real preset files.
//
// Usage:
//   auto root = NtTestHarness::serialiseToDom(plugin.factory(), plugin.alg());
//   bool ok   = NtTestHarness::deserialiseFromDom(plugin.factory(),
//                                                  plugin.alg(), root);
//
// `root` represents the JSON object the plugin populated inside its
// `serialise()` callback. Tests can introspect its structure if they want to
// assert specific field layouts (see test_serialisation.cpp).
// =============================================================================

#pragma once

#include <distingnt/api.h>
#include <distingnt/serialisation.h>
#include <memory>
#include <string>
#include <vector>
#include <utility>

namespace NtTestHarness {

struct JsonValue;
using  JsonValuePtr = std::shared_ptr<JsonValue>;

struct JsonMember {
    std::string  name;
    JsonValuePtr value;
};

struct JsonValue {
    enum Kind { K_NULL, K_BOOL, K_INT, K_FLOAT, K_STRING, K_ARRAY, K_OBJECT };
    Kind                     kind = K_NULL;
    bool                     b    = false;
    int                      i    = 0;
    float                    f    = 0.0f;
    std::string              s;
    std::vector<JsonValuePtr> arr;
    std::vector<JsonMember>  obj;

    static JsonValuePtr makeObject() {
        auto v = std::make_shared<JsonValue>();
        v->kind = K_OBJECT;
        return v;
    }
    static JsonValuePtr makeArray() {
        auto v = std::make_shared<JsonValue>();
        v->kind = K_ARRAY;
        return v;
    }
};

// Run plugin->serialise into a fresh root object. Returns the populated root.
JsonValuePtr serialiseToDom(const _NT_factory* factory, _NT_algorithm* alg);

// Run plugin->deserialise on the given root object. Returns plugin's bool.
bool deserialiseFromDom(const _NT_factory* factory, _NT_algorithm* alg,
                        JsonValuePtr root);

} // namespace NtTestHarness
