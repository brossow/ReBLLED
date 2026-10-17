Import("env")
import os
import shutil

# === Konfiguration ===
ENABLE_MERGE_BIN = True  # Steuert ob Merge ausgeführt wird (OTA .bin.ota wird immer erstellt!)
# ======================

BUILD_DIR = env.subst("$BUILD_DIR")
PROGNAME = env.subst("${PROGNAME}")
APP_BIN = os.path.normpath(os.path.join(BUILD_DIR, f"{PROGNAME}.bin"))
MERGED_BIN = os.path.normpath(os.path.join(BUILD_DIR, f"{PROGNAME}_merged.bin"))
BOARD_CONFIG = env.BoardConfig()

project_name = env.GetProjectOption("custom_project_name") or "Firmware"
version = env.GetProjectOption("custom_version") or "0.0.0"
firmware_filename_merged = f"{project_name}_V{version}.bin"
firmware_filename_ota = f"{project_name}_V{version}.bin.ota"

firmware_path_merged = os.path.normpath(os.path.join(env.subst("$PROJECT_DIR"), ".firmware", firmware_filename_merged))
firmware_path_ota = os.path.normpath(os.path.join(env.subst("$PROJECT_DIR"), ".firmware", firmware_filename_ota))

# Slot size of the partition layout used up to 2.3.0-br.1 (default.csv). A device that
# hasn't been migrated over USB (docs/USB-MIGRATION.md) can only take OTA images below this.
OLD_LAYOUT_APP_SLOT = 0x140000

def warn_if_too_big_for_old_layout(source, target, env):
    size = os.path.getsize(APP_BIN)
    if size > OLD_LAYOUT_APP_SLOT:
        print(f"*** WARNING: firmware is {size} bytes, more than the old 1.25 MB OTA slot "
              f"({OLD_LAYOUT_APP_SLOT} bytes). Devices still on the old partition layout must be "
              f"migrated over USB first (docs/USB-MIGRATION.md); an OTA upload would be refused.")
    else:
        print(f"Firmware {size} bytes: fits the old 1.25 MB OTA slot too "
              f"({OLD_LAYOUT_APP_SLOT - size} bytes spare).")

def copy_bin_as_ota(source, target, env):
    os.makedirs(os.path.dirname(firmware_path_ota), exist_ok=True)
    shutil.copyfile(APP_BIN, firmware_path_ota)
    print(f'Firmware (OTA, uncompressed) copied to: {firmware_path_ota}')

def merge_bin(source, target, env):
    flash_images = env.Flatten(env.get("FLASH_EXTRA_IMAGES", []))
    app_offset = env.subst("$ESP32_APP_OFFSET") or "0x10000"
    flash_images += [app_offset, APP_BIN]

    flash_args = [
        os.path.normpath(env.subst(str(x))) if not str(x).startswith("0x") else str(x)
        for x in flash_images
    ]

    cmd = [
        env.subst("$PYTHONEXE"),
        env.subst("$OBJCOPY"),
        "--chip", BOARD_CONFIG.get("build.mcu", "esp32"),
        "merge_bin",
        "--fill-flash-size", BOARD_CONFIG.get("upload.flash_size", "4MB"),
        "--output", MERGED_BIN,
    ] + flash_args

    command_str = " ".join(f'"{c}"' if " " in c and not c.startswith('"') else c for c in cmd)
    print("Running merge_bin:\n  ", command_str.replace('"', '\"'))
    result = env.Execute(command_str)

    os.makedirs(os.path.dirname(firmware_path_merged), exist_ok=True)
    shutil.copyfile(MERGED_BIN, firmware_path_merged)
    print(f'Firmware (merged) copied to: {firmware_path_merged}')
    return result

# Immer OTA-Datei erzeugen (unkomprimiert, mit Endung .bin.ota)
env.AddPostAction(APP_BIN, copy_bin_as_ota)
env.AddPostAction(APP_BIN, warn_if_too_big_for_old_layout)

# Optional merged .bin erzeugen
if ENABLE_MERGE_BIN:
    env.AddPostAction(APP_BIN, merge_bin)
