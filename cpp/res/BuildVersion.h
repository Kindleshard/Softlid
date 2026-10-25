#pragma once

// This version of BlinkBuddy, in one place: the exe's version resource (app.rc), the app (core/Version.h), the
// installer and the release script all read it from here. (Not "version.h": core/Version.h would find itself.)
#define BB_VERSION_MAJOR 1
#define BB_VERSION_MINOR 0
#define BB_VERSION_PATCH 0
#define BB_VERSION_TEXT "1.0.0"
