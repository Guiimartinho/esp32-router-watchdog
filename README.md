# ESP32 Router Watchdog & Network Monitor

This project transforms an ESP32-S3 into an intelligent hub for monitoring, managing, and ensuring home network connectivity. It's built on the **FreeRTOS** real-time operating system to handle multiple tasks reliably and concurrently.

---

### Core Architectural Features

* **Multi-Tasking RTOS:** Built on FreeRTOS to handle network diagnostics, device discovery, and future modules in parallel without blocking critical operations.
* **Visual Status Indicator:** Utilizes the onboard RGB LED for immediate visual feedback on the system's status (Green for Online, Red for Offline/Failure).
* **Modular C++ Design:** Each core functionality is encapsulated in its own class for better organization and scalability.

---

### Project Modules

#### Module 1: Autonomous Resilience
* `Status:` ✅ **Implemented**
* **Intelligent Recovery:** Attempts to reboot the router via a TR-064 software command for a fast and elegant recovery.
* **Physical Fallback:** Uses a relay to power-cycle the router as a Plan B, ensuring recovery even if the router's software is unresponsive.
* **Current Implementation:** A robust state machine manages the system's status. When an internet outage is confirmed, it follows a progressive timed backoff for reboot attempts (2-min, 30-min, 2-hour intervals).

#### Module 2: Multi-Layered Diagnostics
* `Status:` ✅ **Implemented**
* **Accurate Fault Analysis:** Verifies connectivity through multiple critical points.
* **Informed Decision-Making:** Avoids unnecessary reboots by reliably identifying a true internet outage.
* **Current Implementation:** A resilient two-stage check is performed. It first attempts a lightweight HTTP GET request. Only if that fails, it initiates a burst of ICMP pings to a reliable server (`8.8.8.8`) to confirm the outage before taking action. The system also forces a public DNS to avoid local resolver issues.

#### Module 3: Network Visibility & Discovery
* `Status:` ✅ **Implemented**
* **Active Network Mapping:** Scans the subnet to create a real-time inventory of all connected devices.
* **Service Identification:** Uses mDNS to discover and name services on the network (printers, smart TVs, etc.).
* **Current Implementation:** A dedicated low-priority FreeRTOS task periodically performs a parallel ICMP ping sweep across the entire subnet to discover online hosts without blocking the primary internet monitoring task.

#### Module 4: Security & Traffic Analysis (Promiscuous Mode)
* `Status:` ❌ **Not Implemented**
* **Low-Level Monitoring:** Capture Wi-Fi packet metadata to monitor network health without compromising privacy (encrypted payloads are not inspected).
* **Anomaly Detection:** Identify which devices are generating the most traffic and detect common attacks like de-authentication floods.

#### Module 5: Remote Control Interface (Web Server + API)
* `Status:` ❌ **Not Implemented**
* **Centralized Dashboard:** Host a web interface accessible from any browser on the network to display real-time status, performance graphs, and event logs.
* **On-Demand Control:** Allow manual triggering of reboots and network scans via buttons on the dashboard.
* **Automation Integration:** Expose a RESTful API (`/api/status`, `/api/reboot`) to allow other systems (like Home Assistant, Node-RED) to interact with the monitor.

#### Module 6: Intelligent Notification Gateway
* `Status:` ✅ **Implemented**
* **Proactive Communication:** Sends critical and informative alerts to instant messaging services.
* **Contextual Alerts:** Notifies on system startup, internet outages, and service recovery.
* **Current Implementation:** The system is fully integrated with Telegram. It sends formatted Markdown messages for key events, such as "Super Monitor started", "Internet Outage Detected!", and "Internet Recovered!".

#### Module 7: Active Quality of Service (QoS) Management
* `Status:` ❌ **Not Implemented**
* **Bandwidth Optimization:** Monitor bandwidth consumption and, via TR-064 commands, dynamically prioritize traffic for latency-sensitive applications.
* **Abuse Control:** Ability to limit bandwidth for non-priority devices that are degrading network performance.

#### Module 8: Persistent Logging & Historical Analysis
* `Status:` ❌ **Not Implemented**
* **Long-Term Memory:** Store logs of important events (outages, reboots, latency spikes, new devices) to a MicroSD card.
* **Historical Intelligence:** Allow for the analysis of your connection's stability over weeks or months to identify patterns or provide concrete data to your ISP.