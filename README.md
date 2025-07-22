# ESP32 Router Watchdog & Network Monitor

This project transforms an ESP32-S3 into an intelligent hub for monitoring, managing, and ensuring home network connectivity. It leverages the **FreeRTOS** real-time operating system to handle multiple tasks reliably and concurrently, including Wi-Fi traffic analysis and a web dashboard for control.

---

### Core Architectural Features

* **Multi-Tasking RTOS:** Built on FreeRTOS to handle network diagnostics, device discovery, traffic analysis, and the web server in parallel without blocking critical operations.
* **Visual Status Indicator:** Utilizes the onboard RGB LED for immediate visual feedback on the system's status (Green for Online, Red for Offline/Failure, Purple for provisioning mode).
* **Modular C++ Design:** Each core functionality is encapsulated in its own class for better organization, maintainability, and scalability.

---

### Project Modules

#### Module 1: Autonomous Resilience
* `Status:` ✅ **Implemented**
* **Intelligent Recovery:** Attempts to reboot the router via a TR-064 software command for a fast and elegant recovery.
* **Physical Fallback:** Uses a relay to power-cycle the router as a Plan B, ensuring recovery even if the router's software is unresponsive.
* **Current Implementation:** A robust state machine manages the system's status. When an internet outage is confirmed, it follows a progressive timed backoff for reboot attempts (2-min, 30-min, and 2-hour intervals).

#### Module 2: Multi-Layered Diagnostics
* `Status:` ✅ **Implemented**
* **Accurate Fault Analysis:** Verifies connectivity through multiple critical points.
* **Informed Decision-Making:** Avoids unnecessary reboots by reliably identifying a true internet outage.
* **Current Implementation:** A resilient two-stage check is performed. It first attempts a lightweight HTTP GET request. Only if that fails, it initiates a burst of ICMP pings to a reliable server (`8.8.8.8`). All Ping calls throughout the project are protected by a mutex to ensure stability.

#### Module 3: Network Visibility & Discovery
* `Status:` ✅ **Implemented**
* **Active Network Mapping:** Scans the subnet to create a real-time inventory of all connected devices.
* **Current Implementation:** To ensure maximum system stability, the network scan was refactored from a parallel to a **sequential scan** model. The main task performs a blocking ICMP ping sweep for each IP on the subnet, with small delays to avoid system lock-ups. This eliminates the complexity and instability risks associated with creating multiple network tasks.

#### Module 4: Traffic Analysis & Security (Promiscuous Mode)
* `Status:` ✅ **Implemented**
* **Low-Level Monitoring:** Captures Wi-Fi packets to monitor network health.
* **DNS Query Sniffing:** Filters and decodes specifically DNS queries (UDP port 53), logging which device (by MAC address) is requesting which domain.
* **Current Implementation:** The system periodically enters Sniffer (promiscuous) mode. The capture task is highly optimized to be lightweight, preventing system overload. It uses a hardware filter (`WIFI_PROMIS_FILTER_MASK_DATA`) to receive only data packets and features a **graceful shutdown** mechanism to prevent memory corruption when switching Wi-Fi modes.

#### Module 5: Remote Control Interface (Web Server + API)
* `Status:` ✅ **Implemented**
* **Centralized Dashboard:** Hosts a web interface for real-time status display and on-demand control.
* **Provisioning Portal:** On first boot, it creates an Access Point (AP) with a captive portal for easy configuration of Wi-Fi, router, and Telegram credentials.
* **Current Implementation:** An asynchronous web server provides a dashboard that displays internet status and the list of network devices (via a `/status_json` API). It also allows forcing a router reboot via a button on the interface (via the `/reboot` endpoint).

#### Module 6: Intelligent Notification Gateway
* `Status:` ✅ **Implemented**
* **Proactive Communication:** Sends critical and informative alerts to instant messaging services.
* **Contextual Alerts:** Notifies on system startup, internet outages, and service recovery.
* **Current Implementation:** The system is fully integrated with Telegram. It sends formatted Markdown messages for key events, such as "Super Monitor started," "Internet Outage Detected!", and "Internet Recovered!".

---

### Key Logic & Stability Improvements Implemented

During development and debugging, several architectural improvements were made to ensure system robustness:

* **✅ Graceful Task Shutdown:** The `snifferTask` is now terminated via a control flag (`volatile bool`), allowing it to finish its operations and self-delete safely. This eliminated a critical cause of **Heap Corruption**.

* **✅ Shared Resource Protection (Mutex):** A mutex was implemented for the `ESP32Ping` library. This ensures that different tasks (`NetworkDiscovery` and `NetworkDiagnostics`) can use the ping function without causing **race conditions**, which led to crashes.

* **✅ Robust Wi-Fi State Transitions:** The main `operationalTask` was restructured. Notably, a strategic pause was added after reconnecting to Wi-Fi (when exiting Sniffer mode), giving the ESP32's network stack time to stabilize completely.

* **✅ Increased Stack Memory:** The stack for the main `operationalTask` was increased to 16KB to ensure robust operation and prevent any `Stack Overflows`.

---

### Future Work & TinyML Ideas

#### Planned Modules:
* **Module 7: Active Quality of Service (QoS) Management:** Monitor bandwidth consumption and, via TR-064 commands, dynamically prioritize or limit traffic for specific devices.
* **Module 8: Persistent Logging & Historical Analysis:** Store logs of important events (outages, reboots, latency spikes) to an SD card for long-term stability analysis.

#### Future Modules with Artificial Intelligence (TinyML):
* **🤖 Module 9: Network Anomaly Detection:**
    * **Logic:** Collect network metrics (packets/sec, latency, device count) to train a "normality" model (e.g., an Autoencoder). The model would run on the ESP32, and if the current traffic deviates significantly from the learned normal pattern, an "anomalous behavior" alert would be generated.
    * **Benefit:** Detect issues before they become critical, such as a compromised IoT device generating excessive traffic.

* **🔮 Module 10: Predictive Failure Analysis:**
    * **Logic:** Use a time-series model (e.g., RNN, LSTM) trained on latency, jitter, and packet loss data. The model would learn to recognize the patterns that typically precede an internet outage.
    * **Benefit:** Send a **predictive warning notification**, such as "Warning: Unstable connection detected. A possible outage may occur in the next few minutes," allowing for proactive action.

* **🛡️ Module 11: Device Fingerprinting & Security:**
    * **Logic:** Analyze the traffic characteristics of a new device connecting to the network (protocols, packet sizes, destination servers). A classification model (e.g., Decision Tree, SVM) could identify the device type (phone, laptop, security camera).
    * **Benefit:** Enhance security by sending an alert like "Security Alert: A new, unidentified device (possibly a camera) has connected to the network."
