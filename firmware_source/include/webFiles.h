#pragma once

class WebFiles {
public:
    // Runs one complete on-device Web Files session.
    // Returns after the server and temporary Wi-Fi AP have been stopped.
    static void run();
};
