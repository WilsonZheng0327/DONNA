// Copy this file to secrets.h (same folder) and fill it in.
// secrets.h is gitignored so credentials never end up in the repo.
#pragma once

#define WIFI_SSID "Google-Guest-Legacy"
#define WIFI_PASSWORD ""

// Firebase Realtime Database hostname — from the console it looks like
// https://YOUR-PROJECT-default-rtdb.firebaseio.com/
// Paste ONLY the hostname: no https://, no trailing slash.
#define FIREBASE_HOST "donna-ead10-default-rtdb.firebaseio.com"

// Database secret (legacy token): Firebase console -> Project settings ->
// Service accounts -> Database secrets -> Show. Requests carrying it get
// admin access, so the database rules can deny writes to everyone else.
// Leave "" only if your rules allow public writes (fine for first tests).
#define FIREBASE_AUTH "YIgKFUWPbKL8OIDmqlKlxDFzxlp3DrY9o6VaeGMA"
