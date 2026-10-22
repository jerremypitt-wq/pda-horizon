# PlatformIO pre-script: счётчики UART-трафика панель<-радио для диагностики.
# Патчит ИСХОДНИКИ device-ui в libdeps: байты в UARTClient::receive,
# валидные кадры в SerialClient::handlePacketReceived. Сами счётчики живут
# в Horizon (HorizonView.cpp), здесь только инкремент через block-scope
# extern - никаких заголовков править не надо. Идемпотентно. Fail-open:
# если якоря не найдены (обновление device-ui) - предупреждение в лог,
# сборка продолжается, счётчики останутся нулями.
Import("env")
import glob
import os

PATCHES = [
    ("source/comms/serial/UARTClient.cpp",
     '    if (bytes_read > 0) {\n        ILOG_TRACE("received %d bytes from serial", bytes_read);\n',
     "        { extern volatile uint32_t hzUartRxBytes; hzUartRxBytes += bytes_read; }\n",
     "hzUartRxBytes"),
    ("source/comms/serial/SerialClient.cpp",
     "    meshtastic_FromRadio fromRadio = envelope.decode();\n    if (fromRadio.which_payload_variant != 0) {\n",
     "        { extern volatile uint32_t hzUartRxFrames; hzUartRxFrames++; }\n",
     "hzUartRxFrames"),
]

libdeps = env.subst(os.path.join("$PROJECT_LIBDEPS_DIR", "$PIOENV"))
if not glob.glob(os.path.join(libdeps, "*")):
    print("Horizon: WARNING: libdeps empty, uart monitor not patched")
for sub, anchor, inject, marker in PATCHES:
    cands = glob.glob(os.path.join(libdeps, "*", sub))
    if not cands:
        print(f"Horizon: WARNING: {sub} not found in libdeps, uart monitor partial")
        continue
    for path in cands:
        with open(path, encoding="utf-8") as f:
            s = f.read()
        if marker in s:
            print(f"Horizon: uart monitor already in {path}")
        elif anchor in s:
            s = s.replace(anchor, anchor + inject, 1)
            with open(path, "w", encoding="utf-8", newline="") as f:
                f.write(s)
            print(f"Horizon: uart monitor patched {path}")
        else:
            print(f"Horizon: WARNING: anchor not found in {path}, uart monitor skipped")
