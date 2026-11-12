Machete AI Studio 1.2.4 introduces an interactive on-screen virtual pointer cursor and full-screen cinema gallery on TV, implements DualSense adaptive trigger mechanical resistance, adds one-click Turbo Mode presets with automatic LoRA acceleration (CausVid for Wan 2.1 and Lightning for SDXL), expands the Telegram Companion bot with /turbo mode commands and quick menu toggles, fixes model download errors with comprehensive URL canonicalization, and refines Web Studio header aesthetics.

## Fixes & Enhancements

- **TV UI Virtual Pointer Cursor & Full-Screen Cinema Gallery**:
  - Implemented an interactive on-screen virtual cursor controlled with the DualSense Left Analog Stick or Touchpad, providing smooth point-and-click navigation directly on TV without leaving the homebrew application.
  - Added Full-Screen Cinema Gallery mode (toggled via Square button, canvas click, or thumbnail selection) allowing users to browse their creations full-bleed with L1/R1 navigation, prompt overlays, and favorite toggling.
  - Implemented initial gallery filesystem scanning on console startup (`scan_initial_gallery`), populating existing creations and videos directly into the TV filmstrip.
  - Upgraded the TV filmstrip into a dynamic scrolling window that visually highlights active selections and cursor hover targets across all creations.

- **DualSense Adaptive Trigger Resistance & Haptic Feedback**:
  - Integrated native `scePadSetTriggerEffect` support into the PS5 homebrew pad subsystem.
  - During AI image and video generation, adaptive triggers engage heavy mechanical feedback resistance on L2 and R2, providing physical tactile confirmation that the GPU is under full rendering load.
  - When idle, the R2 trigger engages a two-stage tactile snap (weapon mode) mimicking a professional DSLR camera shutter when pulling the trigger to generate.

- **One-Click Turbo Mode Presets & Accelerator LoRA Bundling**:
  - Added a dedicated "Turbo Mode" toggle button to the Web Studio Create tab.
  - For Wan 2.1 Video: sets 4 steps, CFG 1.0, and auto-activates the CausVid LoRA, slashing video render times from ~15 minutes down to ~2 minutes.
  - For SDXL: sets 8 steps, CFG 1.5, and attaches the SDXL-Lightning LoRA for photorealistic 1024x1024 generations in ~4 seconds.
  - For SD-Turbo / SDXS: sets 1 step, CFG 1.0 for instant sub-second previews.
  - Updated the official model catalog to bundle CausVid LoRA with Wan 2.1 and Lightning LoRA with SDXL.

- **Telegram Companion Bot Turbo Integration**:
  - Added `/turbo`, `/fast`, and `/rapido` slash commands to the Telegram Companion bot.
  - Added a persistent "Modo Rapido (Turbo)" menu button to the Telegram reply keyboard for instant one-tap activation of high-speed parameters.

- **Model Download Reliability & URL Canonicalization**:
  - Corrected model file URLs in the catalog, including SDXS-512 and Wan 2.2 VAE filenames.
  - Enhanced `canonicalize_model_url` in the download core to resolve legacy gated or altered URLs.
  - Updated `downloader_start` to automatically reset failed downloads to queued with cleared errors whenever canonicalized URLs are detected, allowing one-click retry for previously failed queue items.

- **Web Studio Header & Sidebar Polish**:
  - Styled the active model button into an elevated pill badge, preventing layout collisions.
  - Hardened folded sidebar CSS (`html.side-mini`) with strict clipping and hidden labels to prevent text overflow over the top header.
  - Replaced hardcoded dark background gradients in light mode with dynamic theme variables.

## Install

Install from the homebrew store (Machete AI Studio, PPSA99699), or unpack PPSA99699.zip into /data/homebrew/ on the console and let ShadowMountPlus scan. Requirements: kstuff-lite, ShadowMountPlus, elfldr; ftpsrv on 2121 optional.

Credits: PS5SD by Darkatek7, stable-diffusion.cpp (ggerganov / leejet), ps5-exporter (Marice), Machete Studio additions by VenezuelanBiggie24.

----

Machete AI Studio 1.2.4 presenta un cursor de puntero virtual en pantalla y galeria de cine a pantalla completa en TV, implementa resistencia mecanica en los gatillos adaptativos del DualSense, anade preajustes de Modo Rapido (Turbo) con un solo clic con aceleracion LoRA automatica (CausVid para Wan 2.1 y Lightning para SDXL), expande el bot complementario de Telegram con comandos /turbo y botones de menu rapido, soluciona errores de descarga de modelos mediante canonicalizacion integral de URLs y perfecciona la estetica de la cabecera en el Web Studio.

## Correcciones y Mejoras

- **Cursor Virtual en Pantalla y Galeria de Cine a Pantalla Completa en TV**:
  - Implementacion de un cursor interactivo en pantalla controlado con el stick analogico izquierdo o el panel tactil del DualSense, permitiendo navegacion completa sin salir de la aplicacion ni requerir navegadores externos.
  - Modo de Galeria a Pantalla Completa (activable con el boton Cuadrado, haciendo clic en el lienzo o seleccionando miniaturas), permitiendo recorrer todas las creaciones a pantalla completa con L1/R1, indicaciones en pantalla y marcado de favoritos.
  - Escaneo automatico del sistema de archivos al iniciar la consola (`scan_initial_gallery`), cargando todas las imagenes y videos previos en la tira de miniaturas de la TV.
  - Tira de miniaturas mejorada con ventana desplazable que resalta la seleccion activa y el objetivo sobrevolado por el cursor.

- **Resistencia en Gatillos Adaptativos del DualSense y Respuesta Haptica**:
  - Integracion nativa de `scePadSetTriggerEffect` en el subsistema de mando para homebrew de PS5.
  - Durante la generacion de imagen o video, los gatillos adaptativos L2 y R2 activan una resistencia mecanica firme, confirmando fisicamente que la GPU esta en pleno proceso de renderizado.
  - En reposo, el gatillo R2 activa una respuesta de dos etapas con tope mecanico (modo weapon) que imita el obturador de una camara profesional al disparar para generar.

- **Preajustes de Modo Rapido (Turbo) con LoRA Acelerador**:
  - Boton dedicado "Modo Rapido (Turbo)" en la pestana Crear de la interfaz web.
  - Para Wan 2.1 Video: configura 4 pasos, CFG 1.0 y vincula automaticamente el LoRA CausVid, reduciendo el tiempo de renderizado de ~15 minutos a ~2 minutos.
  - Para SDXL: configura 8 pasos, CFG 1.5 y adjunta el LoRA SDXL-Lightning para imagenes fotorrealistas de 1024x1024 en ~4 segundos.
  - Para SD-Turbo / SDXS: configura 1 paso y CFG 1.0 para resultados instantaneos.
  - Actualizacion del catalogo oficial para incluir los LoRAs CausVid y Lightning en las descargas correspondientes.

- **Integracion de Modo Turbo en el Bot de Telegram**:
  - Comandos `/turbo`, `/fast` y `/rapido` anadidos al bot de Telegram.
  - Boton persistente "Modo Rapido (Turbo)" en el teclado interactivo de Telegram para activar ajustes veloces con un solo toque.

- **Fiabilidad en Descargas de Modelos y Canonicalizacion de URLs**:
  - Correccion de URLs de modelos en el catalogo, incluyendo nombres de archivo de SDXS-512 y Wan 2.2 VAE.
  - Mejora de `canonicalize_model_url` para resolver URLs antiguas con restricciones.
  - Actualizacion en `downloader_start` para reiniciar descargas fallidas al estado en cola y limpiar errores cuando se detecten URLs canonicalizadas.

- **Perfeccionamiento Visual en Web Studio**:
  - Transformacion del selector de modelo activo en una capsula destacada para evitar colisiones visuales.
  - Refuerzo del CSS de la barra lateral plegada (`html.side-mini`) con recorte estricto para evitar superposicion de textos en la cabecera.
  - Eliminacion de manchas oscuras en tema claro sustituyendo gradientes fijos por variables de tema.

## Instalacion

Instala desde la tienda homebrew (Machete AI Studio, PPSA99699) o descomprime PPSA99699.zip en /data/homebrew/ en la consola y permite que ShadowMountPlus escanee. Requisitos: kstuff-lite, ShadowMountPlus, elfldr; ftpsrv en 2121 opcional.

Creditos: PS5SD por Darkatek7, stable-diffusion.cpp (ggerganov / leejet), ps5-exporter (Marice), adiciones de Machete Studio por VenezuelanBiggie24.
