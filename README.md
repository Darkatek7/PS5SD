<div align="center">

<img src="assets/machete-icon.png" alt="Machete PS5SD Icon" width="160" height="160" style="border-radius: 28px; box-shadow: 0 8px 24px rgba(0,0,0,0.5);">

# Machete PS5SD: Image & Video Studio

**Local AI-Powered Image and Video Generation on the PlayStation 5 GPU**  
*Built on stable-diffusion.cpp and ggml-vulkan over RADV, featuring a responsive web UI and TV dashboard.*

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![Platform: PS5](https://img.shields.io/badge/Platform-PS5%20(7.00--13.60)-purple.svg)](#requirements)
[![Video Generation](https://img.shields.io/badge/AI-Text--to--Video%20%7C%20Image--to--Video-00e5ff.svg)](#video-generation-t2v--i2v)
[![Upstream](https://img.shields.io/badge/Upstream-Darkatek7%2FPS5SD-brightgreen.svg)](https://github.com/Darkatek7/PS5SD)

</div>

---

## Table of Contents

- [Overview](#overview)
- [New Features in v0.9.7 (Machete Studio Suite)](#new-features-in-v097-machete-studio-suite)
- [New Features in v0.9.6](#new-features-in-v096)
- [Face Studio Identity Lock](#face-studio-identity-lock)
- [Multi-Storage Engine and USB Drives](#multi-storage-engine-and-usb-drives)
- [Remote Telegram Companion Bot](#remote-telegram-companion-bot)
- [Thermal Guardian and Fan Safety](#thermal-guardian-and-fan-safety)
- [Video Generation Models](#video-generation-models)
- [Live Console Telemetry](#live-console-telemetry)
- [TV Dashboard and Instant QR Connection](#tv-dashboard-and-instant-qr-connection)
- [Hardware Architecture and Memory Budget](#hardware-architecture-and-memory-budget)
- [Requirements](#requirements)
- [Installation Guide](#installation-guide)
- [Network Configuration and Environment Variables](#network-configuration-and-environment-variables)
- [HTTP REST API and Client Integration](#http-rest-api-and-client-integration)
- [Credits and Acknowledgments](#credits-and-acknowledgments)
- [License](#license)
- [Version en Espanol](#machete-ps5sd-estudio-de-imagen-y-video)

---

## Overview

Machete PS5SD is a complete generative artificial intelligence suite designed specifically to leverage the unified memory architecture of jailbroken PlayStation 5 consoles.

Building on the foundation created by Darkatek7 in [PS5SD](https://github.com/Darkatek7/PS5SD), this Machete release integrates full video generation (Text-to-Video and Image-to-Video), hardware telemetry, memory defragmentation, accelerated decoding, and automated crash diagnostics.

---

## New Features in v0.9.7 (Machete Studio Suite)

- **Face Studio (1-Click Identity Lock)**: Upload a portrait once and generate images while preserving facial identity and likeness automatically using native PhotoMaker integration or image guidance.
- **External USB Storage and Dynamic Path Engine**: Store models and outputs on external USB drives (/mnt/usb0 through /mnt/usb7) or console storage (/data/MacheteAI). Auto-scans and reuses existing models in /data/PS5SD/models without data duplication or deletion during clean app reinstalls.
- **HTTP Range Download Resumption**: Large multi-gigabyte model downloads now support HTTP 206 Range headers. Interrupted downloads resume from their last downloaded byte without restarting from scratch.
- **Thermal Guardian Safety Alerts**: Real-time configurable temperature alerts (default 80 C) pulse red on both TV and Web when the SoC approaches high heat thresholds.
- **TV Ambient Art Screensaver**: Automatically showcases generated artworks in full screen after 60 seconds of inactivity on the console TV.
- **Remote Telegram Companion Bot**: Generate images and inspect console telemetry remotely from your smartphone via Telegram while your console runs in the background.
- **Zero-Hardcoded Environment Independence**: Elimination of rigid hardcoded paths across the entire engine, enabling coexistence alongside upstream PS5SD (PPSA99576) and Machete (PPSA99699).

---

## New Features in v0.9.6

- **Video Generation (Wan 2.2, Wan 2.1, LTX-Video)**: Generate video sequences up to 81 frames with Wan 2.2 TI2V 5B, Wan 2.1 T2V 1.3B, and LTX-Video 2B.
- **CausVid Video Acceleration**: Generate a 2-second Wan video (33 frames at 832x480) in approximately 3 minutes and 40 seconds with 4 steps and CFG 1, compared to ~15 minutes standard.
- **Fast Video Decoding (TAEHV)**: Decodes 33 frames in 3.6 seconds instead of 55 seconds using the optimized TAEHV autoencoder.
- **Live Hardware Telemetry**: Real-time display of SoC temperature, total power consumption in watts, cooling fan drive percentage, and CPU/GPU load on the TV dashboard and in the web UI.
- **ControlNet SDXL**: Guide image layout with Canny edge detection directly on the console.
- **Modernized Web UI**: Modular interface featuring desktop sidebar tabs, mobile bottom navigation bar, pinch/drag zoom in gallery, search tags (#tag), Danbooru tag completions, and background queue processing.
- **Diagnostic Logging and Crash Recovery**: Automatic dual-location logs (/data/homebrew/PPSA99699/app.log) and atomic crash reports (/data/homebrew/PPSA99699/crash.log) with POSIX signal handlers (SIGSEGV, SIGBUS, SIGABRT, SIGFPE, SIGILL).

---

## Face Studio Identity Lock

Face Studio brings 1-click portrait face preservation to the PS5 GPU:
1. Navigate to the **Create** tab in the web interface.
2. Under the prompt section, toggle **Face Studio (Identity Lock)** on.
3. Click **Upload Face Photo** (or drag and drop a clear headshot photo into the avatar area).
4. Enter any prompt describing a scene or outfit (for example: *a brave cybernetic knight standing in neon rain*).
5. When `photomaker-v1.safetensors` is downloaded, Machete automatically activates PhotoMaker identity embedding. If running a standard model, Machete automatically applies facial reference guidance.

---

## Multi-Storage Engine and USB Drives

Machete PS5SD eliminates rigid filesystem dependencies and protects your downloaded models from accidental deletion:
- **Clean Reinstall Safety**: Models are saved to `/data/MacheteAI/models` (or `/data/PS5SD/models` if already existing). If you delete or cleanly reinstall the homebrew title (`/data/homebrew/PPSA99699`), all downloaded models and outputs remain completely safe.
- **USB Drive Storage**: Connect any FAT32 or exFAT USB drive to the PS5.
  1. Open the **Settings** tab in the web UI.
  2. Under **Storage & USB Devices**, check **Enable External USB Drives**.
  3. Click **Scan** to detect connected drives (`/mnt/usb0` through `/mnt/usb7`).
  4. Ensure your USB drive contains the directory structure `MacheteAI/models/` or `models/`.
  5. Click **Use this storage** on your preferred USB mount. Machete will immediately switch active paths without restarting the console.

---

## Remote Telegram Companion Bot

Run your PS5 as an autonomous personal generative AI server accessible from your phone:
1. Message `@BotFather` on Telegram to create a bot and get an API Token.
2. In the Machete Web UI, navigate to the **Settings** tab.
3. Scroll to **Remote Companion (Telegram)** and check **Enable Telegram Companion Bot**.
4. Paste your Bot Token and optional Chat ID (to restrict access exclusively to yourself).
5. Click **Save Companion Settings**.
6. From your phone on Telegram, send `/status` to check temperatures and load, or send `/gen a beautiful cosmic nebula in 4k` to generate images on your console GPU from anywhere.

---

## Thermal Guardian and Fan Safety

Sony factory firmware fan curves target ~91 C APU temperatures and keep fan speeds low. To ensure your console APU is safeguarded during heavy compute loads:
- **Thermal Warning Alerts**: Set your warning threshold in the **Settings** tab (default: 80 C).
- When the SoC exceeds the threshold, the header chip on the Web UI turns bright red with a pulsating beacon, and the TV dashboard displays a high-visibility warning `ALERT: SoC >= 80C`.
- **TV Ambient Art Screensaver**: If the console sits idle for 60 seconds, the TV screen dims status text and smoothly transitions into a full-screen ambient art gallery showcasing your generated images.

---

## Video Generation Models

| Model | Type | Architecture / Quant | VRAM / RAM | Speed / Steps |
|---|---|---|---|---|
| Wan 2.2 TI2V 5B | Image-to-Video / T2V | QuantStack Q8_0 + UMT5 Q4_K_M + Wan 2.2 VAE | ~10.5 GB | 33 frames at 832x480 in ~6 min (or ~16 min at 1280x704) |
| Wan 2.1 T2V 1.3B (Fast Decoder) | Text-to-Video | FP16 + UMT5 Q4_K_M + TAEHV Decoder | ~6.7 GB | 33 frames in ~1.5 min with CausVid LoRA (3.6s decode) |
| LTX-Video 2B Distilled | Text-to-Video | Distilled Q8_0 + T5-XXL Q4_K_M | ~5.0 GB | 8-12 steps distilled, 33 frames at 768x512 |

---

## Live Console Telemetry

When ps5-exporter is running via the console ELF loader (port 9021), Machete PS5SD monitors system sensors:
- **SoC Temperature**: Highest reading across internal APU thermal sensors.
- **Power**: Total system SoC consumption in watts.
- **Fan**: Pulse-width drive percentage (0 to 100%).
- **CPU / GPU Load**: Percentage of active cycles utilized during diffusion and VAE decoding.

---

## TV Dashboard and Instant QR Connection

The on-screen TV dashboard renders:
- Current routable console IPv4 address and active listening port.
- Hardware-accelerated QR code for pairing mobile devices without manual IP typing.
- Live system status, active model, memory pool utilization, and telemetry metrics.

---

## Hardware Architecture and Memory Budget

The PS5 features 16 GiB of GDDR6 unified memory shared between CPU and GPU. Under jailbreak environments, approximately 11.0 to 11.5 GiB are available for direct memory allocations.

Machete PS5SD manages this budget via:
- Contiguous GPU buffer defragmentation in RADV.
- Block-based spatial and temporal VAE tiling.
- Sequential text encoder offloading prior to diffusion sampling.

---

## Requirements

- PlayStation 5 Console running compatible firmware (7.00 through 13.60).
- Jailbreak environment: etaHEN with kstuff-lite and ShadowMountPlus.
- elfldr (port 9021) active for ps5-exporter telemetry.
- Optional: ftpsrv running on port 2121 or 2122 for remote file access.

---

## Installation Guide

### Option 1: PS5 Homebrew Catalog (Recommended)
Search for **Machete AI Studio** (Title ID: `PPSA99699`) in the PS5 Homebrew Store and install with one click.

### Option 2: Manual Installation via ZIP
1. Download `PPSA99699.zip` from [Releases](https://github.com/VenezuelanBiggie24/Machete-PS5SD-IMG-and-VID/releases).
2. Extract the archive into `/data/homebrew/` on your console:
   `/data/homebrew/PPSA99699/eboot.bin`
3. Trigger a title scan in ShadowMountPlus.

---

## Network Configuration and Environment Variables

Configuration options can be placed in `/data/PS5SD/env.txt`:
- `PS5SD_ADDRESS=<ip>`: Manually override the console IP address displayed on screen.
- `PS5SD_PORT=7860`: Set custom HTTP server port.
- `PS5SD_DL_CONNECTIONS=16`: Parallel download threads for the model catalog.

---

## HTTP REST API and Client Integration

Machete PS5SD exposes standard endpoints compatible with AUTOMATIC1111 WebUI and OpenAI API:
- `GET /ps5sd/v1/app/info`: System version, build hash, and stable-diffusion.cpp revision.
- `POST /sdcpp/v1/img_gen`: Image generation with optional ControlNet and inpainting.
- `POST /sdcpp/v1/vid_gen`: Video generation pipeline (returns WebM format).
- `GET /ps5sd/v1/gallery`: Query and manage generated media files and tags.

---

## Credits and Acknowledgments

- **Darkatek7**: Creator of [PS5SD](https://github.com/Darkatek7/PS5SD).
- **ggerganov & leejet**: Authors of [stable-diffusion.cpp](https://github.com/leejet/stable-diffusion.cpp).
- **Marice**: Author of [ps5-exporter](https://github.com/Marice/ps5-exporter).
- **cobanov**: Author of [PS5LM](https://github.com/cobanov/PS5LM).
- **John Tornblom & drakmor**: Authors of [ftpsrv](https://github.com/drakmor/ftpsrv).

---

## License

This project is licensed under the GNU General Public License v3.0 (GPL-3.0). See [LICENSE](LICENSE) for details.

---
---

# Machete PS5SD: Estudio de Imagen y Video

**Generacion Local de Imagen y Video con IA en la GPU de PlayStation 5**  
*Basado en stable-diffusion.cpp y ggml-vulkan sobre RADV, con interfaz web responsiva y dashboard para TV.*

---

## Tabla de Contenidos

- [Resumen](#resumen)
- [Nuevas Caracteristicas en v0.9.7 (Machete Studio Suite)](#nuevas-caracteristicas-en-v097-machete-studio-suite)
- [Nuevas Caracteristicas en v0.9.6](#nuevas-caracteristicas-en-v096)
- [Face Studio: Bloqueo de Identidad y Retrato](#face-studio-bloqueo-de-identidad-y-retrato)
- [Motor Multi-Almacenamiento y Unidades USB](#motor-multi-almacenamiento-y-unidades-usb)
- [Bot Acompanante Remoto de Telegram](#bot-acompanante-remoto-de-telegram)
- [Guardian Termico y Seguridad de Ventilador](#guardian-termico-y-seguridad-de-ventilador)
- [Modelos de Generacion de Video](#modelos-de-generacion-de-video)
- [Telemetria de Consola en Vivo](#telemetria-de-consola-en-vivo)
- [Dashboard de TV y Conexion Instantanea QR](#dashboard-de-tv-y-conexion-instantanea-qr)
- [Presupuesto de Memoria y Rendimiento](#presupuesto-de-memoria-y-rendimiento)
- [Requisitos](#requisitos-1)
- [Guia de Instalacion](#guia-de-instalacion)
- [Variables de Entorno](#variables-de-entorno)
- [API REST y Clientes](#api-rest-y-clientes)
- [Creditos y Agradecimientos](#creditos-y-agradecimientos)
- [Licencia](#licencia)

---

## Resumen

Machete PS5SD es una suite completa de inteligencia artificial generativa disenada especificamente para aprovechar la arquitectura de memoria unificada en consolas PlayStation 5 liberadas.

Construido sobre las bases creadas por Darkatek7 en [PS5SD](https://github.com/Darkatek7/PS5SD), esta version de Machete integra generacion de video completa (Texto a Video e Imagen a Video), telemetria de hardware, desfragmentacion de memoria, decodificacion acelerada y diagnostico automatico de fallos.

---

## Nuevas Caracteristicas en v0.9.7 (Machete Studio Suite)

- **Face Studio (Bloqueo de Identidad en 1 Clic)**: Sube una foto de tu rostro y genera imagenes conservando tu identidad facial de manera automatica mediante integracion nativa con PhotoMaker o guia facial.
- **Almacenamiento USB Externo y Rutas Dinamicas**: Guarda modelos y resultados en memorias USB externas (/mnt/usb0 a /mnt/usb7) o en el almacenamiento interno (/data/MacheteAI). Escanea y reutiliza modelos existentes en /data/PS5SD/models sin duplicar datos ni perderlos al reinstalar la aplicacion.
- **Reanudacion de Descargas por Rango HTTP**: Las descargas pesadas de varios gigabytes admiten ahora cabeceras HTTP Range 206. Si la conexion se interrumpe, la descarga continua exactamente donde se quedo.
- **Guardian Termico con Alertas en Vivo**: Alertas visuales configurables (80 C por defecto) que parpadean en rojo en la TV y la web cuando el SoC alcanza limites elevados de temperatura.
- **Salvapantallas Ambiental para TV**: Muestra automaticamente una galeria de arte a pantalla completa con las creaciones generadas tras 60 segundos de inactividad en la consola.
- **Bot Acompanante Remoto en Telegram**: Genera imagenes y consulta telemetria y temperaturas directamente desde tu celular en Telegram mientras la consola esta encendida.
- **Independencia Total de Entorno y Rutas**: Eliminacion de rutas fijas ("hardcoded"), garantizando la coexistencia pacifica entre la app original (PPSA99576) y Machete (PPSA99699).

---

## Nuevas Caracteristicas en v0.9.6

- **Generacion de Video (Wan 2.2, Wan 2.1, LTX-Video)**: Secuencias de video de hasta 81 fotogramas con Wan 2.2 TI2V 5B, Wan 2.1 T2V 1.3B y LTX-Video 2B.
- **Aceleracion de Video CausVid**: Genera un video Wan de 2 segundos (33 frames a 832x480) en aproximadamente 3 minutos y 40 segundos con 4 pasos y CFG 1, frente a los ~15 minutos habituales.
- **Decodificacion Rapida de Video (TAEHV)**: Decodifica 33 fotogramas en 3.6 segundos en lugar de 55 segundos mediante el autoencoder optimizado TAEHV.
- **Telemetria de Hardware en Vivo**: Monitorizacion en tiempo real de temperatura del SoC, consumo de potencia en vatios, porcentaje del ventilador y uso de CPU/GPU en el dashboard de TV y en la web.
- **ControlNet SDXL**: Guiado visual de composicion mediante deteccion de bordes Canny directamente en la consola.
- **Interfaz Web Renovada**: Diseno modular con pestanas laterales en PC, navegacion inferior en moviles, zoom en galeria, etiquetas (#tag), autocompletado Danbooru y cola de tareas en segundo plano.
- **Registro de Fallos y Diagnostico**: Registro continuo en /data/homebrew/PPSA99699/app.log y generacion atomica de crash.log ante excepciones o senales POSIX (SIGSEGV, SIGBUS, SIGABRT, SIGFPE, SIGILL).

---

## Face Studio: Bloqueo de Identidad y Retrato

Face Studio permite preservar el rostro en tus generaciones con un solo clic:
1. Ve a la pestana **Create** en la interfaz web.
2. Debajo del campo de prompt, activa la casilla **Face Studio (Identity Lock)**.
3. Pulsa **Upload Face Photo** (o arrastra una foto clara de tu cara al area del avatar).
4. Escribe cualquier prompt describiendo la escena (por ejemplo: *un astronauta valiente flotando en el espacio profundo*).
5. Si tienes descargado `photomaker-v1.safetensors`, Machete activara automaticamente PhotoMaker para maxima fidelidad. Con modelos estandar, aplicara guiado de preservacion facial.

---

## Motor Multi-Almacenamiento y Unidades USB

Machete PS5SD elimina la dependencia rigida del sistema de archivos y protege tus modelos de borrados accidentales:
- **Seguridad ante Reinstalaciones Limpias**: Los modelos se descargan en `/data/MacheteAI/models` (o en `/data/PS5SD/models` si ya existia). Si borras la app para actualizar en limpio (`/data/homebrew/PPSA99699`), tus modelos descargados permaneceran intactos.
- **Almacenamiento en USB Externo**: Conecta un pendrive o disco USB (FAT32 o exFAT) a la PS5.
  1. Abre la pestana **Settings** en la web.
  2. En **Storage & USB Devices**, marca **Enable External USB Drives**.
  3. Pulsa **Scan** para detectar las unidades conectadas (`/mnt/usb0` a `/mnt/usb7`).
  4. Tu memoria USB debe contener la carpeta `MacheteAI/models/` o `models/` con los modelos dentro.
  5. Pulsa **Use this storage** en la unidad deseada para cambiar el almacenamiento activo inmediatamente sin reiniciar la consola.

---

## Bot Acompanante Remoto de Telegram

Convierte tu PS5 en un servidor personal de inteligencia artificial accesible desde cualquier lugar:
1. Escribe a `@BotFather` en Telegram para crear un bot y obtener un Token de API.
2. En la web de Machete, entra en la pestana **Settings**.
3. Baja hasta **Remote Companion (Telegram)** y marca **Enable Telegram Companion Bot**.
4. Pega tu Bot Token y opcionalmente tu Chat ID para restringir el uso solo a ti.
5. Pulsa **Save Companion Settings**.
6. Desde Telegram en tu celular, envia `/status` para ver temperaturas y estado, o envia `/gen un hermoso atardecer en las montanas` para generar en la GPU de tu consola desde cualquier lugar.

---

## Guardian Termico y Seguridad de Ventilador

Las curvas de ventilador de fabrica de Sony buscan mantener el APU en torno a ~91 C y mantienen revoluciones bajas. Para garantizar que tu consola no sufra sobrecalentamiento en sesiones pesadas de renderizado:
- **Alerta Termica Configurable**: Establece el limite de advertencia en **Settings** (por defecto: 80 C).
- Si el SoC alcanza o supera dicho limite, el indicador superior en la web se tornara rojo con pulsos de advertencia y el dashboard de TV mostrara `ALERT: SoC >= 80C`.
- **Salvapantallas Ambiental en TV**: Si dejas la aplicacion inactiva durante 60 segundos, la TV atenuara el texto de estado y activara una presentacion visual a pantalla completa con las imagenes que has creado.

---

## Modelos de Generacion de Video

| Modelo | Tipo | Arquitectura / Cuantizacion | VRAM / RAM | Velocidad / Pasos |
|---|---|---|---|---|
| Wan 2.2 TI2V 5B | Imagen a Video / T2V | QuantStack Q8_0 + UMT5 Q4_K_M + Wan 2.2 VAE | ~10.5 GB | 33 frames a 832x480 en ~6 min (o ~16 min a 1280x704) |
| Wan 2.1 T2V 1.3B (Decodificador Rapido) | Texto a Video | FP16 + UMT5 Q4_K_M + Decodificador TAEHV | ~6.7 GB | 33 frames en ~1.5 min con LoRA CausVid (3.6s decodificacion) |
| LTX-Video 2B Distilled | Texto a Video | Distilled Q8_0 + T5-XXL Q4_K_M | ~5.0 GB | 8-12 pasos destilados, 33 frames a 768x512 |

---

## Telemetria de Consola en Vivo

Cuando ps5-exporter esta activo a traves del cargador ELF (puerto 9021), Machete PS5SD monitoriza:
- **Temperatura SoC**: Registro del sensor termico mas caliente del procesador.
- **Potencia**: Consumo global del chip en vatios.
- **Ventilador**: Porcentaje de modulacion de velocidad (0 a 100%).
- **Carga CPU / GPU**: Porcentaje de tiempo de computo durante difusion y procesado VAE.

---

## Dashboard de TV y Conexion Instantanea QR

El panel en pantalla muestra:
- Direccion IPv4 enrutada real y puerto de escucha activo.
- Codigo QR acelerado por hardware para sincronizar moviles sin escribir la IP manualmente.
- Estado del sistema, modelo cargado, memoria directa aprovechable y telemetria.

---

## Presupuesto de Memoria y Rendimiento

La consola dispone de 16 GiB de memoria GDDR6 unificada compartida entre CPU y GPU. En consolas liberadas, entre 11.0 y 11.5 GiB quedan disponibles para ejecucion directa.

Machete PS5SD gestiona este presupuesto mediante:
- Desfragmentacion de buffers contiguos de GPU en el controlador RADV.
- Division por bloques (tiling) espacial y temporal en el VAE.
- Descarga secuencial de codificadores de texto previa al muestreo.

---

## Requisitos

- Consola PlayStation 5 en firmware compatible (7.00 hasta 13.60).
- Entorno de liberacion: etaHEN con kstuff-lite y ShadowMountPlus.
- elfldr activo en puerto 9021 para telemetria de ps5-exporter.
- Opcional: servidor ftpsrv en puerto 2121 o 2122 para transferencia de archivos.

---

## Guia de Instalacion

### Opcion 1: Tienda Homebrew de PS5 (Recomendado)
Busca **Machete AI Studio** (Title ID: `PPSA99699`) en la tienda oficial de Homebrew e instala en un clic.

### Opcion 2: Instalacion Manual mediante ZIP
1. Descarga `PPSA99699.zip` desde [Releases](https://github.com/VenezuelanBiggie24/Machete-PS5SD-IMG-and-VID/releases).
2. Descomprime el contenido en `/data/homebrew/` en tu consola:
   `/data/homebrew/PPSA99699/eboot.bin`
3. Ejecuta un escaneo de aplicaciones en ShadowMountPlus.

---

## Variables de Entorno

Puedes configurar opciones en `/data/PS5SD/env.txt`:
- `PS5SD_ADDRESS=<ip>`: Forzar manualmente la direccion IP mostrada en pantalla.
- `PS5SD_PORT=7860`: Cambiar el puerto HTTP del servidor.
- `PS5SD_DL_CONNECTIONS=16`: Hilos simultaneos de descarga para el catalogo.

---

## API REST y Clientes

Machete PS5SD expone rutas compatibles con AUTOMATIC1111 y OpenAI:
- `GET /ps5sd/v1/app/info`: Version, identificador de build y version de stable-diffusion.cpp.
- `POST /sdcpp/v1/img_gen`: Generacion de imagenes con soporte para ControlNet e inpainting.
- `POST /sdcpp/v1/vid_gen`: Generacion de video nativo en formato WebM.
- `GET /ps5sd/v1/gallery`: Consulta y gestion de imagenes, videos y etiquetas.

---

## Creditos y Agradecimientos

- **Darkatek7**: Autor y creador de [PS5SD](https://github.com/Darkatek7/PS5SD).
- **ggerganov y leejet**: Desarrolladores de [stable-diffusion.cpp](https://github.com/leejet/stable-diffusion.cpp).
- **Marice**: Autor de [ps5-exporter](https://github.com/Marice/ps5-exporter).
- **cobanov**: Creador de [PS5LM](https://github.com/cobanov/PS5LM).
- **John Tornblom y drakmor**: Creadores de [ftpsrv](https://github.com/drakmor/ftpsrv).

---

## Licencia

Este proyecto esta publicado bajo la Licencia Publica General de GNU v3.0 (GPL-3.0). Consulta [LICENSE](LICENSE) para mas detalles.
