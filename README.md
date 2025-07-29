# ESP32 Router Watchdog & Intelligent Network Monitor

This project transforms an ESP32-S3 into an intelligent hub for monitoring, managing, and ensuring home network connectivity. It leverages the **FreeRTOS** real-time operating system to handle multiple tasks reliably and concurrently, including Wi-Fi traffic analysis, a web dashboard, and **anomaly detection with Artificial Intelligence (TinyML)**.

---

### Core Architectural Features

* **Multi-Tasking RTOS:** Built on FreeRTOS to handle network diagnostics, device discovery, traffic analysis, and the web server in parallel without blocking critical operations.
* **Visual Status Indicator:** Utilizes the onboard RGB LED for immediate visual feedback on the system's status (Green for Online, Red for Sniffer Mode, Purple for provisioning).
* **Modular C++ Design:** Each core functionality is encapsulated in its own class for better organization, maintainability, and scalability.

---

### Project Modules

#### Module 1: Autonomous Resilience
* `Status:` ✅ **Implemented**
* **Intelligent Recovery:** Attempts to reboot the router via a TR-064 software command for a fast and elegant recovery.
* **Physical Fallback:** Uses a relay to power-cycle the router as a Plan B, ensuring recovery even if the router's software is unresponsive.
* **Current Implementation:** A robust state machine manages the system's status. When an internet outage is confirmed, it follows a progressive timed backoff for reboot attempts.

#### Module 2: Multi-Layered Diagnostics
* `Status:` ✅ **Implemented**
* **Accurate Fault Analysis:** Verifies connectivity through multiple critical points to avoid unnecessary reboots.
* **Current Implementation:** A resilient two-stage check is performed. It first attempts a lightweight HTTP GET request. Only if that fails, it initiates a burst of ICMP pings to a reliable server (`8.8.8.8`). All Ping calls throughout the project are protected by a mutex to ensure stability.

#### Module 3: Network Visibility & Discovery
* `Status:` ✅ **Implemented**
* **Active Network Mapping:** Scans the subnet to create a real-time inventory of all connected devices.
* **Current Implementation:** To ensure maximum system stability, the network scan was refactored from a parallel to a **sequential scan** model. The main task performs a blocking ICMP ping sweep for each IP on the subnet, eliminating instability risks associated with creating multiple network tasks.

#### Module 4: Traffic Analysis (Promiscuous Mode)
* `Status:` ✅ **Implemented**
* **Low-Level Monitoring:** Captures Wi-Fi packets to monitor network health.
* **DNS Query Sniffing:** Filters and decodes specifically DNS queries (UDP port 53), logging which device is requesting which domain.
* **Current Implementation:** The system periodically enters Sniffer mode. The capture task is highly optimized to be lightweight, using a hardware filter (`WIFI_PROMIS_FILTER_MASK_DATA`) and a **graceful shutdown** mechanism to prevent memory corruption.

#### Module 5: Remote Control Interface (Web Server + API)
* `Status:` ✅ **Implemented**
* **Centralized Dashboard:** Hosts a web interface for real-time status display and control.
* **Provisioning Portal:** Creates an Access Point (AP) with a captive portal for easy Wi-Fi configuration on first use.
* **Current Implementation:** An asynchronous web server provides a dashboard that displays internet status and network devices (via `/status_json` API) and allows forcing a router reboot (via `/reboot` endpoint).

#### Module 6: Intelligent Notification Gateway
* `Status:` ✅ **Implemented**
* **Proactive Communication:** Sends critical and informative alerts to Telegram.
* **Current Implementation:** The system sends formatted Markdown messages for key events like system startup, outages, recovery, and anomaly alerts.

#### Module 7: Device & Service Discovery (UPnP)
* `Status:` ⚠️ **Partially Implemented**
* **Service Discovery:** Uses the UPnP protocol to discover compatible devices (routers, printers, etc.) and list their services.
* **Current Implementation:** The `TinyUPnP` library has been integrated and modified to use `esp_log`. A periodic task discovers and logs UPnP-compatible devices on the network.

#### Module 8: Persistent Logging & Historical Analysis
* `Status:` ❌ **Not Implemented**
* **Long-Term Memory:** Store logs of important events (outages, reboots, anomalies) to an SD card for long-term stability analysis.

#### 🤖 Module 9: Network Anomaly Detection with TinyML
* `Status:` ✅ **Implemented**
* **Embedded Artificial Intelligence:** Utilizes an Autoencoder neural network, trained with TensorFlow and running directly on the ESP32, to detect anomalous traffic patterns.
* **Benefit:** Detects issues that simple rules cannot, such as unusual traffic volume for a given pattern, potentially indicating unauthorized downloads or malicious activity.
* **Current Implementation:** After each Sniffer mode cycle, aggregated metrics (`packet_count` and `total_bytes`) are collected and fed into the TinyML model. If the model's "reconstruction error" exceeds a pre-calculated threshold, the system identifies an anomaly and sends an alert via Telegram.

---

### Key Architectural Improvements & Stability Fixes

* **✅ Graceful Task Shutdown:** The `snifferTask` is now terminated via a control flag, allowing it to self-delete safely. This eliminated a critical cause of **Heap Corruption**.
* **✅ Shared Resource Protection (Mutex):** A mutex ensures that different tasks (`NetworkDiscovery`, `NetworkDiagnostics`) can use the `ESP32Ping` library without causing **race conditions**.
* **✅ Robust Wi-Fi State Transitions:** A strategic pause was added after reconnecting Wi-Fi (when exiting Sniffer mode), giving the ESP32's network stack time to stabilize.
* **✅ Increased Stack Memory:** The stack for the main `operationalTask` was increased to 16KB to ensure robust operation and prevent `Stack Overflows`.

---

### What's Next: Future Work

* **Module 10: Predictive Failure Analysis (TinyML):**
    * **Logic:** Use a time-series model (e.g., RNN, LSTM) trained on latency and packet loss data to predict an internet outage before it happens.
    * **Benefit:** Send a predictive warning notification, allowing for proactive action.

* **Module 11: Device Fingerprinting & Security (TinyML):**
    * **Logic:** Analyze the traffic characteristics of a new device to classify its type (phone, laptop, security camera) using a classification model.
    * **Benefit:** Enhance security by sending an alert when a new, unidentified device connects to the network.