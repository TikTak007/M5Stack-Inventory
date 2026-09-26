#include <ArduinoJson.h>
#include <cassert>
#include <iostream>
#include "SaveStatus.h"

bool accepted(const char* reply, int httpCode = 200) {
  JsonDocument response;
  if (deserializeJson(response, reply)) return false;
  return isVerifiedSaveAckJson(httpCode, response, "event-a");
}
int main() {
  assert(accepted(R"({"ok":true,"eventId":"event-a","verified":true,"duplicate":false})"));
  assert(accepted(R"({"ok":true,"eventId":"event-a","verified":true,"duplicate":true})"));
  assert(accepted(R"({"ok":true,"eventId":"event-a","verified":true})"));
  assert(!accepted(R"({"ok":true,"eventId":"event-a","duplicate":true})"));
  assert(!accepted(R"({"ok":true,"eventId":"event-a","verified":false,"duplicate":true})"));
  assert(!accepted(R"({"ok":true,"eventId":"event-b","verified":true})"));
  assert(!accepted(R"({"ok":true,"eventId":"event-a","verified":"true"})"));
  assert(!accepted(R"({"ok":true,"eventId":"event-a","verified":1})"));
  assert(!accepted(R"({"ok":1,"eventId":"event-a","verified":true})"));
  assert(!accepted(R"({"ok":true,"eventId":null,"verified":true})"));
  assert(!accepted(R"({"ok":true,"eventId":12,"verified":true})"));
  assert(!accepted(R"({"ok":false,"eventId":"event-a","verified":true})"));
  assert(!accepted(R"({"ok":true,"eventId":"event-a","verified":true})", 503));
  assert(!accepted("{"));
  assert(!accepted(""));
  assert(!accepted("[]"));
  std::cout << "Production ArduinoJson typed ACK checks passed\n";
}
