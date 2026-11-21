#pragma once
struct cJSON;
bool provision_open(cJSON* reply);bool provision_apply(const char* hex);

void provision_status(cJSON* reply);
