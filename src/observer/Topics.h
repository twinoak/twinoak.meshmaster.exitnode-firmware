// The topics an observer publishes to, from a template with three placeholders - read from
// agessaman/MeshCore, branch observer-firmware, d664d89df66bdacc1319900a2e2d152fa31b9dc9:
//
//   {iata}     mqtt.iata, uppercase                             MQTTBridge.cpp:956-964
//   {device}   the node's public key, 64 uppercase hex           examples/simple_repeater/MyMesh.cpp:1222-1226
//   {type}     "status" or "packets"                             MQTTTopicRouter.h:30-38 ("raw" and "neighbors" are not published here)
//
// A preset slot, and a custom slot with no template of its own, publish to
// meshcore/{iata}/{device}/{type}, and only once mqtt.iata is a real code - three letters or
// digits, and not the placeholder "XXX" (MQTTTopicRouter.h:67-71 with MQTTObserverValidation.h:15-24).
// A custom slot with a template substitutes the three placeholders and copies anything else
// through as it is, an unknown "{...}" included (MQTTTopicTemplate.h:14-56); a result that does
// not fit, or is empty, is no topic at all, never a truncated one.
//
// Host-portable.

#pragma once

#include <cstddef>

namespace mm {
namespace observer {

enum class MessageType { Status, Packets };

constexpr const char* kDefaultTopic = "meshcore/{iata}/{device}/{type}";
constexpr size_t kTopicCap = 128;  // the observer firmware's own topic buffers (MQTTBridge.cpp:3114, :4109)

const char* typeName(MessageType type);

// True when `iata` is a code the meshcore layout takes: three letters or digits, not "XXX".
bool iataUsable(const char* iata);

// The topic into `out`: true when it was built whole. `tmpl` nullptr or "" means the default
// layout, with its iata check; anything else is substituted as the custom slots do.
bool buildTopic(const char* tmpl, const char* iata, const char* device, MessageType type, char* out, size_t cap);

}  // namespace observer
}  // namespace mm
