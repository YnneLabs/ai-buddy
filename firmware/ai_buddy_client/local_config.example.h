// Copy this file to local_config.h and set values only on the flashing machine.
// local_config.h is ignored by Git so device credentials cannot be committed.
#define AI_BUDDY_DEFAULT_WIFI_SSID ""
#define AI_BUDDY_DEFAULT_WIFI_PASSWORD ""
#define AI_BUDDY_DEFAULT_BACKEND_URL "http://192.168.1.28:8000"
#define AI_BUDDY_DEFAULT_DEVICE_TOKEN "local-development-token"

// Optional one-time migration for a provisioned device. Both values must match.
// #define AI_BUDDY_BACKEND_MIGRATE_FROM "http://192.168.1.28:8000"
// #define AI_BUDDY_BACKEND_MIGRATE_TO "https://ai-buddy.evil-gamer.net"
