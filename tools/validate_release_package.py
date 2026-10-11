#!/usr/bin/env python3
"""
PS5 Library Browser - Release Package & ELF Validator
Validates zip archive contents, PS5 homebrew metadata (sce_sys/param.json),
and ELF binary architecture (64-bit, Little-Endian, x86_64, executable/PIE).
"""

import sys
import os
import zipfile
import json
import struct
import re

def log_check(name, passed, details=""):
    status = "PASS" if passed else "FAIL"
    print(f"[{status}] {name}")
    if details:
        for line in details.strip().split("\n"):
            print(f"       {line}")
    return passed

def validate_elf_header(elf_bytes):
    if len(elf_bytes) < 64:
        return False, f"File too small for 64-bit ELF header ({len(elf_bytes)} bytes)"

    magic = elf_bytes[:4]
    if magic != b"\x7fELF":
        return False, f"Invalid ELF magic: {magic}"

    ei_class = elf_bytes[4]      # 1=32-bit, 2=64-bit
    ei_data = elf_bytes[5]       # 1=Little-endian, 2=Big-endian
    ei_version = elf_bytes[6]    # 1=Current version
    ei_osabi = elf_bytes[7]      # 0=System V, 9=FreeBSD

    e_type = int.from_bytes(elf_bytes[16:18], "little")
    e_machine = int.from_bytes(elf_bytes[18:20], "little")
    e_version = int.from_bytes(elf_bytes[20:24], "little")
    e_entry = int.from_bytes(elf_bytes[24:32], "little")
    e_phoff = int.from_bytes(elf_bytes[32:40], "little")
    e_phnum = int.from_bytes(elf_bytes[56:58], "little")

    issues = []
    if ei_class != 2:
        issues.append(f"Expected 64-bit ELF (class 2), got class {ei_class}")
    if ei_data != 1:
        issues.append(f"Expected Little-Endian (data 1), got {ei_data}")
    if ei_version != 1 or e_version != 1:
        issues.append(f"Unexpected ELF version (ei_version={ei_version}, e_version={e_version})")
    if e_machine != 0x3E:
        issues.append(f"Expected x86_64 architecture (machine 0x3E / 62), got 0x{e_machine:02X}")
    # PS5 payloads and PIE executables use ET_DYN (3) or ET_EXEC (2)
    if e_type not in (2, 3):
        issues.append(f"Expected EXEC (2) or DYN (3), got type {e_type}")
    if e_phoff == 0 or e_phnum == 0:
        issues.append(f"No program headers found (phoff={e_phoff}, phnum={e_phnum})")

    type_name = "DYN (Position Independent Executable)" if e_type == 3 else "EXEC (Executable)"
    osabi_name = "FreeBSD" if ei_osabi == 9 else ("System V" if ei_osabi == 0 else f"ABI {ei_osabi}")
    details = (
        f"Architecture: 64-bit Little-Endian (x86_64, 0x3E)\n"
        f"ELF Type: {type_name}\n"
        f"OS/ABI: {osabi_name} ({ei_osabi})\n"
        f"Entry Point: 0x{e_entry:X}, Program Headers: {e_phnum} at offset 0x{e_phoff:X}"
    )

    if issues:
        return False, "\n".join(issues)
    return True, details

def validate_param_json(param_bytes):
    try:
        data = json.loads(param_bytes.decode("utf-8"))
    except Exception as e:
        return False, f"Invalid JSON syntax in sce_sys/param.json: {e}"

    required_fields = ["titleId", "contentId", "contentVersion", "localizedParameters"]
    missing = [f for f in required_fields if f not in data]
    if missing:
        return False, f"Missing required fields: {', '.join(missing)}"

    title_id = data.get("titleId", "")
    content_id = data.get("contentId", "")
    content_ver = data.get("contentVersion", "")

    # PS5 Title IDs: PPSA / CUSA / FAKE / LAPY followed by numbers or alphanumeric
    if not re.match(r"^[A-Z0-9_]{9,}$", title_id):
        return False, f"Invalid titleId format: '{title_id}'"

    loc_params = data.get("localizedParameters", {})
    def_lang = loc_params.get("defaultLanguage", "en-US")
    title_name = loc_params.get(def_lang, {}).get("titleName", "")
    if not title_name:
        title_name = data.get("titleName", "")
    if not title_name:
        return False, f"Missing titleName in localizedParameters[{def_lang}]"

    details = (
        f"Title Name: {title_name}\n"
        f"Title ID: {title_id}\n"
        f"Content ID: {content_id}\n"
        f"Version: {content_ver} (Default Lang: {def_lang})"
    )
    return True, details

def validate_package(archive_path):
    print(f"==================================================")
    print(f"  Validating PS5 Release Package: {os.path.basename(archive_path)}")
    print(f"==================================================")

    all_passed = True

    # 1. Archive existence and readability
    if not os.path.exists(archive_path):
        log_check("Archive File Exists", False, f"File not found: {archive_path}")
        return False

    archive_size = os.path.getsize(archive_path)
    if archive_size == 0:
        log_check("Archive Non-Empty", False, "Archive is 0 bytes")
        return False
    log_check("Archive Readable", True, f"Size: {archive_size:,} bytes")

    try:
        with zipfile.ZipFile(archive_path, "r") as zf:
            file_list = zf.namelist()
            log_check("ZIP File Integrity", True, f"{len(file_list)} entries in archive")

            # 2. Required files check
            required_files = [
                "ps5-library-browser.elf",
                "sce_sys/param.json",
                "README.md",
                "LICENSE"
            ]
            missing_files = []
            for rf in required_files:
                # normalize path separators
                matched = any(f == rf or f.replace("\\", "/") == rf for f in file_list)
                if not matched:
                    missing_files.append(rf)

            if missing_files:
                log_check("Required Files Present", False, f"Missing files: {', '.join(missing_files)}")
                all_passed = False
            else:
                log_check("Required Files Present", True, ", ".join(required_files))

            # 3. Check sce_sys/param.json
            param_path = next((f for f in file_list if f == "sce_sys/param.json" or f.endswith("param.json")), None)
            if param_path:
                param_bytes = zf.read(param_path)
                ok, details = validate_param_json(param_bytes)
                log_check("PS5 Metadata (sce_sys/param.json)", ok, details)
                if not ok:
                    all_passed = False
            else:
                log_check("PS5 Metadata (sce_sys/param.json)", False, "sce_sys/param.json not found")
                all_passed = False

            # 4. Check ELF binary architecture
            elf_path = next((f for f in file_list if f.endswith(".elf")), None)
            if elf_path:
                elf_bytes = zf.read(elf_path)
                ok, details = validate_elf_header(elf_bytes)
                log_check(f"ELF Architecture Validation ({elf_path})", ok, details)
                if not ok:
                    all_passed = False
            else:
                log_check("ELF Binary Present", False, "No .elf file in archive")
                all_passed = False

    except zipfile.BadZipFile as e:
        log_check("ZIP Archive Valid", False, f"Corrupted or invalid zip file: {e}")
        return False
    except Exception as e:
        log_check("Package Inspection", False, f"Unexpected error during validation: {e}")
        return False

    print("--------------------------------------------------")
    if all_passed:
        print("RESULT: ALL VALIDATION CHECKS PASSED [OK]")
    else:
        print("RESULT: VALIDATION FAILED - ISSUES FOUND [ERROR]")
    print("--------------------------------------------------")
    return all_passed

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <path-to-release-archive.zip>")
        sys.exit(2)

    archive = sys.argv[1]
    success = validate_package(archive)
    sys.exit(0 if success else 1)
