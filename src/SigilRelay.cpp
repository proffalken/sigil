#include "SigilRelay.h"
#include <Preferences.h>

#ifdef SIGIL_OTEL_ENABLED
#include <WiFi.h>
#include <WiFiManager.h>
#include <OtelEmbeddedCpp.h>
#include <time.h>
#endif

// ── Constructor ────────────────────────────────────────────────────────────

SigilRelay::SigilRelay(const char* relayId)
    : _relayId(relayId)
    , _rs485(nullptr)
    , _device(nullptr)
    , _attrs()
    , _debug(nullptr)
    , _pendingTxReadyMs(0)
    , _lastRs485ActivityMs(0)
{
#ifdef SIGIL_OTEL_ENABLED
    _pendingIsRegister = false;
    _otelReady         = false;
    _msgsUpstream      = 0;
    _msgsDownstream    = 0;
    _msgsDropped       = 0;
    _registrationsSeen = 0;
    _lastMessageMs     = 0;
    _lastMetricsMs     = 0;
    _metricsIntervalMs = 60000;
    _startMs           = 0;
#endif
}

// ── Public API ─────────────────────────────────────────────────────────────

void SigilRelay::addAttribute(const char* key, const char* value) {
    _attrs.add(key, value);
}

void SigilRelay::setDebugStream(Stream& stream) {
    _debug = &stream;
    _debugln("[sigil] debug enabled");
}

#ifdef SIGIL_OTEL_ENABLED
void SigilRelay::setMetricsInterval(uint32_t ms) {
    _metricsIntervalMs = ms;
}
#endif

void SigilRelay::begin(HardwareSerial& rs485Serial,  uint32_t rs485Baud,  int rs485Rx,  int rs485Tx,
                       HardwareSerial& deviceSerial, uint32_t deviceBaud, int deviceRx, int deviceTx) {
    _rs485  = &rs485Serial;
    _device = &deviceSerial;

    _rs485->begin (rs485Baud,  SERIAL_8N1, rs485Rx,  rs485Tx);
    _device->begin(deviceBaud, SERIAL_8N1, deviceRx, deviceTx);

    // Flush any startup noise before the end device begins transmitting
    delay(100);
    while (_device->available()) _device->read();

    // Load any previously persisted attributes (location, area, etc.)
    _loadConfig();

    _debugln("[sigil] relay ready");

#ifdef SIGIL_OTEL_ENABLED
    _initOtel();
#endif
}

void SigilRelay::update() {
    // String::indexOf() stops at embedded NUL bytes, which appear in bootloader
    // noise from the end device. This helper scans byte-by-byte via operator[]
    // so it is unaffected by nulls embedded in the string data.
    auto findBrace = [](const String& s) -> int {
        for (unsigned int i = 0; i < s.length(); i++) {
            if (s[i] == '{') return (int)i;
        }
        return -1;
    };

    bool rs485HasData = _rs485 && _rs485->available();
    if (rs485HasData) _lastRs485ActivityMs = millis();

    // --- Device → RS485 bus ---
    // Skip reading a new line while one is still queued for the bus — the
    // pogo-pin UART buffers it, nothing is lost, only delayed.
    if (_pendingTx.length() == 0 && _device && _device->available()) {
        String raw = _device->readStringUntil('\n');
        _debugln("[sigil] rx from device: " + raw);

        int jsonStart = findBrace(raw);
        if (jsonStart != -1) {
            if (jsonStart > 0) raw = raw.substring(jsonStart);
            _forwardToRS485(raw);
        } else {
            _debugln("[sigil] no JSON found in device message");
        }
    }

    _flushPendingTx();

    // --- RS485 bus → Device ---
    if (rs485HasData) {
        String raw = _rs485->readStringUntil('\n');
        _debugln("[sigil] rx from RS485: " + raw);

        int jsonStart = findBrace(raw);
        if (jsonStart != -1) {
            if (jsonStart > 0) raw = raw.substring(jsonStart);

            // Config messages addressed to this relay are consumed here —
            // not forwarded to the end device.
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, raw);
            if (!err
                    && strcmp(doc["msg_type"] | "", "config") == 0
                    && strcmp(doc["device_id"] | "", _relayId)  == 0) {
                _handleConfig(doc);
            } else {
                _forwardToDevice(raw);
            }
        } else {
            _debugln("[sigil] no JSON found in RS485 message");
        }
    }

#ifdef SIGIL_OTEL_ENABLED
    if (_otelReady && _metricsIntervalMs > 0) {
        unsigned long now = millis();
        if ((now - _lastMetricsMs) >= _metricsIntervalMs) {
            _emitMetrics();
            _lastMetricsMs = now;
        }
    }
#endif
}

// ── Private helpers ────────────────────────────────────────────────────────

void SigilRelay::_forwardToRS485(const String& json) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);

    if (err) {
        _debugln("[sigil] JSON parse error: " + String(err.c_str()));
        _debugln("[sigil] failed input: " + json);
#ifdef SIGIL_OTEL_ENABLED
        _msgsDropped++;
#endif
        return;
    }

    // Protocol fields at top level
    doc["relay_id"] = _relayId;
    doc["relay_ts"] = millis();

    // Merge relay attributes into the "attributes" object.
    // Any device attributes already present are preserved;
    // relay values win on key collision.
    _attrs.applyTo(doc);

#ifdef SIGIL_OTEL_ENABLED
    if (_otelReady) {
        // Extract the device's W3C traceparent from the message attributes.
        // If present, create a child span for the relay hop and inject the
        // relay's own traceparent so the admin panel continues the same trace.
        String tp;
        if (doc["attributes"].is<JsonObject>()) {
            tp = doc["attributes"]["traceparent"].as<String>();
        }
        OTel::ExtractedContext extracted;
        if (tp.length() > 0 && OTel::parseTraceparent(tp, extracted)) {
            // Install the device span as the active parent context (RAII).
            OTel::RemoteParentScope parentScope(extracted.ctx);
            // This span is a child of the device's puzzle.state span.
            OTel::Span relaySpan("sigil.relay.forward");
            relaySpan
                .setKind(OTel::SpanKind::SERVER)  // displays as CLIENT in Dash0
                .setAttribute("relay.id", String(_relayId))
                .setAttribute("sigil.msg_type", String(doc["msg_type"] | ""))
                .setAttribute("device.id",      String(doc["device_id"] | ""));
            // Replace device traceparent with relay's — admin panel becomes child of relay.
            OTel::Propagators::inject([&doc](const char* k, const char* v) {
                doc["attributes"][k] = v;
            });
            relaySpan.setOk();
            // relaySpan + parentScope destruct here (LIFO), restoring context
        }
    }
#endif

    String output;
    serializeJson(doc, output);
    output += '\n';

    // Queue rather than write immediately — see "RS485 bus contention" in
    // SigilRelay.h. _flushPendingTx() sends it once the bus is clear.
    _pendingTx        = output;
    _pendingTxReadyMs = millis() + _jitterMs();
    _debugln("[sigil] queued for RS485: " + output);

#ifdef SIGIL_OTEL_ENABLED
    _pendingIsRegister = strcmp(doc["msg_type"] | "", "register") == 0;
#endif
}

bool SigilRelay::_busClear(unsigned long now) const {
    if (_rs485 && _rs485->available()) return false; // mid-frame, someone's still sending
    return (now - _lastRs485ActivityMs) >= SIGIL_BUS_QUIET_MS;
}

uint32_t SigilRelay::_jitterMs() const {
    return sigilHash(_relayId) % (SIGIL_BUS_JITTER_MAX_MS + 1);
}

void SigilRelay::_flushPendingTx() {
    if (_pendingTx.length() == 0) return;

    unsigned long now = millis();
    if (now < _pendingTxReadyMs) return;
    if (!_busClear(now)) return;

    _rs485->print(_pendingTx);
    _debugln("[sigil] forwarded to RS485: " + _pendingTx);

#ifdef SIGIL_OTEL_ENABLED
    _msgsUpstream++;
    _lastMessageMs = now;
    if (_pendingIsRegister) _registrationsSeen++;
#endif

    _pendingTx = "";
}

void SigilRelay::_forwardToDevice(const String& json) {
    if (_device) {
        _device->print(json + '\n');
        _debugln("[sigil] forwarded to device: " + json);
#ifdef SIGIL_OTEL_ENABLED
        _msgsDownstream++;
#endif
    }
}

void SigilRelay::_debugln(const String& msg) {
    if (_debug) _debug->println(msg);
}

// ── Config messages and NVS persistence ───────────────────────────────────

void SigilRelay::_loadConfig() {
    Preferences prefs;
    prefs.begin("sigil_r", /*readOnly=*/true);
    String json = prefs.getString("attrs", "{}");
    prefs.end();

    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return;

    for (JsonPairConst kv : doc.as<JsonObjectConst>()) {
        _attrs.add(kv.key().c_str(), kv.value().as<const char*>());
    }
    _debugln("[sigil] loaded persisted config");
}

void SigilRelay::_persistConfig() {
    // Serialise current attributes to a JSON object string and save to NVS.
    JsonDocument tmp;
    _attrs.applyTo(tmp);                         // writes into tmp["attributes"]
    String json;
    serializeJson(tmp["attributes"], json);

    Preferences prefs;
    prefs.begin("sigil_r", /*readOnly=*/false);
    prefs.putString("attrs", json);
    prefs.end();
}

void SigilRelay::_handleConfig(JsonDocument& doc) {
    JsonObjectConst incoming = doc["attributes"].as<JsonObjectConst>();
    if (incoming.isNull()) {
        _debugln("[sigil] config message has no attributes — ignored");
        return;
    }

    for (JsonPairConst kv : incoming) {
        _attrs.add(kv.key().c_str(), kv.value().as<const char*>());
    }

    _persistConfig();
    _debugln("[sigil] config applied and persisted");
}

// ── OTel (compiled only when SIGIL_OTEL_ENABLED) ──────────────────────────

#ifdef SIGIL_OTEL_ENABLED

void SigilRelay::_initOtel() {
    // ── WiFi via WiFiManager ───────────────────────────────────────────────
    // On first boot (or after a reset), WiFiManager opens a captive-portal AP
    // named "Sigil-<relayId>". Connect to it with any device, enter your WiFi
    // credentials, and they are saved to NVS. Subsequent boots connect silently.
    WiFiManager wm;
    wm.setConfigPortalTimeout(180);   // close portal after 3 min if unused

    String apName = "Sigil-" + String(_relayId);
    _debugln("[sigil:otel] starting WiFiManager — AP: " + apName);

    bool connected = wm.autoConnect(apName.c_str());
    if (!connected) {
        _debugln("[sigil:otel] WiFi not configured or timed out — OTel disabled");
        _otelReady = false;
        return;
    }

    _debugln("[sigil:otel] WiFi connected: " + WiFi.localIP().toString());

    // ── NTP sync ───────────────────────────────────────────────────────────
    // otel-embedded-cpp uses gettimeofday() for nanosecond timestamps;
    // without NTP the timestamps will be wrong but the relay will still work.
    _debugln("[sigil:otel] syncing NTP...");
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");

    time_t now      = 0;
    uint8_t retries = 0;
    while (now < 1609459200UL && retries++ < 20) {
        delay(500);
        time(&now);
    }
    if (now < 1609459200UL) {
        _debugln("[sigil:otel] NTP sync failed — timestamps may be inaccurate");
    } else {
        _debugln("[sigil:otel] NTP synced");
    }

    // ── OTel resource + metrics init ───────────────────────────────────────
    auto& res = OTel::defaultResource();
    res.set("service.name",        "sigil-relay");
    res.set("service.instance.id", _relayId);

    OTel::Tracer::begin("sigil-relay", "0.4.0");
    OTel::Metrics::begin("sigil", "0.4.0");
    OTel::Metrics::setDefaultMetricLabel("relay_id", _relayId);

    _startMs       = millis();
    _lastMetricsMs = _startMs;
    _otelReady     = true;
    _debugln("[sigil:otel] OTel ready");
}

void SigilRelay::_emitMetrics() {
    unsigned long now = millis();

    // Counters — monotonically increasing, cumulative totals
    OTel::Metrics::sum("sigil.relay.messages.upstream",
                       (double)_msgsUpstream,   true, "CUMULATIVE", "1");
    OTel::Metrics::sum("sigil.relay.messages.downstream",
                       (double)_msgsDownstream, true, "CUMULATIVE", "1");
    OTel::Metrics::sum("sigil.relay.messages.dropped",
                       (double)_msgsDropped,    true, "CUMULATIVE", "1");
    OTel::Metrics::sum("sigil.relay.registrations",
                       (double)_registrationsSeen, true, "CUMULATIVE", "1");

    // Gauges — instantaneous snapshots
    OTel::Metrics::gauge("sigil.relay.uptime_seconds",
                         (double)(now - _startMs) / 1000.0, "s");

    if (_lastMessageMs > 0) {
        OTel::Metrics::gauge("sigil.relay.last_message_age_ms",
                             (double)(now - _lastMessageMs), "ms");
    }
}

#endif // SIGIL_OTEL_ENABLED
