#!/usr/bin/env python3
"""Packs the not64 Wii U build as an installable title (WUP).

The title is laid out the way Cemu and Loadiine read it (``code``, ``content``,
``meta``). With ``--nuspacker`` and ``--key`` it is also encrypted into the
format WUP Installer GX2 installs. The common key belongs to the user and is
never bundled here.

Games are never packed into the title. ``--sd-dir`` writes the files that go on
the SD card (``not64/autoboot.txt`` and, with ``--rom``, a copy of your ROM).

Example::

    tools/make_wup.py executables/not64-wiiu.rpx out/not64
    tools/make_wup.py executables/not64-wiiu.rpx out/conker --name "Conker's Bad Fur Day" \\
        --title-id 000500001e64c0ab --rom ~/Roms/conker.z64 --sd-dir out/sd
"""
from __future__ import annotations

import argparse
import logging
import re
import shutil
import subprocess
import sys
from pathlib import Path
from xml.sax.saxutils import escape

logger = logging.getLogger("make_wup")

DEFAULT_TITLE_ID = "000500001e64e000"
LANGUAGES = ("ja", "en", "fr", "de", "it", "es", "zhs", "ko", "nl", "pt", "ru", "zht")
TITLE_ID_RE = re.compile(r"^[0-9a-f]{16}$")
RPX_NAME = "not64.rpx"
XML_HEAD = '<?xml version="1.0" encoding="utf-8"?>\n'


def _hex(tag: str, length: int, value: str) -> str:
    return f'  <{tag} type="hexBinary" length="{length}">{value}</{tag}>\n'


def app_xml(title_id: str) -> str:
    """Returns ``code/app.xml``: identity of the title."""
    return (
        XML_HEAD
        + '<app type="complex" access="777">\n'
        + '  <version type="unsignedInt" length="4">16</version>\n'
        + _hex("os_version", 8, "000500101000400A")
        + _hex("title_id", 8, title_id.upper())
        + _hex("title_version", 2, "0000")
        + '  <sdk_version type="unsignedInt" length="4">21204</sdk_version>\n'
        + _hex("app_type", 4, "8000002E")
        + _hex("group_id", 4, "00000400")
        + _hex("os_mask", 32, "0" * 64)
        + _hex("common_id", 8, "0" * 16)
        + "</app>\n"
    )


def cos_xml(codegen_bytes: int = 0x01000000) -> str:
    """Returns ``code/cos.xml``: what the OS lends the process.

    ``codegen_size`` and ``codegen_core`` are what let the PowerPC recompiler
    exist on a console: without them ``OSGetCodegenVirtAddrRange`` reports no
    area (see docs/DYNAREC.md). The codegen core is core 0.
    """
    zero4 = "00000000"
    out = (
        XML_HEAD
        + '<app type="complex" access="777">\n'
        + '  <version type="unsignedInt" length="4">1</version>\n'
        + '  <cmdFlags type="unsignedInt" length="4">0</cmdFlags>\n'
        + '  <argstr type="string" length="4096"></argstr>\n'
        + _hex("title_version", 2, "0000")
        + _hex("os_version", 8, "000500101000400A")
        + _hex("max_size", 4, "40000000")
        + _hex("avail_size", 4, zero4)
        + _hex("codegen_size", 4, f"{codegen_bytes:08X}")
        + _hex("codegen_core", 4, "80000001")
        + _hex("max_codesize", 4, "00800000")
        + _hex("overlay_arena", 4, zero4)
    )
    for stack in range(3):
        out += _hex(f"default_stack{stack}_size", 4, zero4)
        out += _hex(f"default_redzone{stack}_size", 4, zero4)
        out += _hex(f"exception_stack{stack}_size", 4, zero4)
    out += '  <permissions type="complex">\n'
    out += '    <p0 type="complex">\n'
    out += _hex("group", 4, "00000001").replace("  <", "      <", 1)
    out += _hex("mask", 8, "FFFFFFFFFFFFFFFF").replace("  <", "      <", 1)
    out += "    </p0>\n"
    for index in range(1, 19):
        out += f'    <p{index} type="complex">\n'
        out += _hex("group", 4, zero4).replace("  <", "      <", 1)
        out += _hex("mask", 8, "0" * 16).replace("  <", "      <", 1)
        out += f"    </p{index}>\n"
    out += "  </permissions>\n</app>\n"
    return out


def meta_xml(name: str, publisher: str, title_id: str) -> str:
    """Returns ``meta/meta.xml``: what the Wii U menu shows."""
    long_name = escape(name)
    pub = escape(publisher)
    out = (
        XML_HEAD
        + '<menu type="complex" access="777">\n'
        + '  <version type="unsignedInt" length="4">33</version>\n'
        + '  <product_code type="string" length="32">WUP-N-NOT6</product_code>\n'
        + '  <content_platform type="string" length="32">WUP</content_platform>\n'
        + '  <company_code type="string" length="8">0001</company_code>\n'
        + '  <mastering_date type="string" length="32"></mastering_date>\n'
        + '  <logo_type type="unsignedInt" length="4">0</logo_type>\n'
        + _hex("app_launch_type", 4, "00000000")
        + _hex("invisible_flag", 4, "00000000")
        + _hex("no_managed_flag", 4, "00000000")
        + _hex("no_event_log", 4, "00000000")
        + _hex("no_icon_database", 4, "00000000")
        + _hex("launching_flag", 4, "00000004")
        + _hex("install_flag", 4, "00000000")
        + '  <closing_msg type="unsignedInt" length="4">0</closing_msg>\n'
        + '  <title_version type="unsignedInt" length="4">0</title_version>\n'
        + _hex("title_id", 8, title_id.upper())
        + _hex("group_id", 4, "00000400")
        + _hex("boss_id", 8, "0" * 16)
        + _hex("os_version", 8, "000500101000400A")
        + _hex("app_size", 8, "0" * 16)
        + _hex("common_boss_size", 8, "0" * 16)
        + _hex("account_boss_size", 8, "0" * 16)
        + _hex("save_size", 8, "0" * 16)
        + _hex("common_save_size", 8, "0" * 16)
        + _hex("account_save_size", 8, "0" * 16)
    )
    for lang in LANGUAGES:
        out += f'  <longname_{lang} type="string" length="512">{long_name}</longname_{lang}>\n'
        out += f'  <shortname_{lang} type="string" length="256">{long_name}</shortname_{lang}>\n'
        out += f'  <publisher_{lang} type="string" length="256">{pub}</publisher_{lang}>\n'
    return out + "</menu>\n"


def _save_tga(src: Path, dst: Path, size: tuple[int, int], alpha: bool) -> None:
    """Writes ``src`` as the uncompressed TGA the Wii U menu expects."""
    from PIL import Image  # imported late: only packing needs Pillow

    with Image.open(src) as img:
        img = img.convert("RGBA" if alpha else "RGB").resize(size, Image.LANCZOS)
        img.save(dst, format="TGA")


def build_title(rpx: Path, out: Path, name: str, publisher: str, title_id: str, meta_dir: Path) -> None:
    """Creates the ``code``/``content``/``meta`` tree for one title in ``out``."""
    if not TITLE_ID_RE.match(title_id):
        raise ValueError(f"title id must be 16 lowercase hex digits, got {title_id!r}")
    if not rpx.is_file():
        raise FileNotFoundError(rpx)

    if out.exists():
        shutil.rmtree(out)
    for sub in ("code", "content", "meta"):
        (out / sub).mkdir(parents=True)

    shutil.copyfile(rpx, out / "code" / RPX_NAME)
    (out / "code" / "app.xml").write_text(app_xml(title_id), encoding="utf-8")
    (out / "code" / "cos.xml").write_text(cos_xml(), encoding="utf-8")
    (out / "meta" / "meta.xml").write_text(meta_xml(name, publisher, title_id), encoding="utf-8")
    # an installable title needs a non-empty content folder
    (out / "content" / "not64.txt").write_text("not64 for Wii U\n", encoding="utf-8")

    _save_tga(meta_dir / "icon.png", out / "meta" / "iconTex.tga", (128, 128), alpha=True)
    _save_tga(meta_dir / "tv_splash.png", out / "meta" / "bootTvTex.tga", (1280, 720), alpha=False)
    _save_tga(meta_dir / "drc_splash.png", out / "meta" / "bootDrcTex.tga", (854, 480), alpha=False)
    logger.info("title written to %s", out)


ROM_MAGICS = {b"\x80\x37\x12\x40": "z64", b"\x37\x80\x40\x12": "v64", b"\x40\x12\x37\x80": "n64"}


def rom_format(rom: Path) -> str | None:
    """Returns ``z64``, ``v64`` or ``n64`` from the ROM's first four bytes, or None when unknown."""
    with rom.open("rb") as fh:
        return ROM_MAGICS.get(fh.read(4))


def write_sd_files(sd_dir: Path, rom: Path | None, autoboot_rom: str | None) -> None:
    """Writes ``not64/autoboot.txt`` (and your ROM, when given) for the SD card."""
    base = sd_dir / "not64"
    (base / "roms").mkdir(parents=True, exist_ok=True)
    if rom is not None:
        if not rom.is_file():
            raise FileNotFoundError(rom)
        fmt = rom_format(rom)
        if fmt is None:
            logger.warning("%s does not look like an N64 ROM (unknown header)", rom.name)
        else:
            logger.info("ROM byte order: %s", fmt)
        safe = re.sub(r"[^A-Za-z0-9._-]", "-", rom.name)
        shutil.copyfile(rom, base / "roms" / safe)
        autoboot_rom = autoboot_rom or f"roms/{safe}"
    if autoboot_rom:
        (base / "autoboot.txt").write_text(autoboot_rom + "\n", encoding="utf-8")
        logger.info("autoboot: %s", autoboot_rom)


def encrypt(title_dir: Path, out: Path, nuspacker: Path, key: Path) -> None:
    """Runs NUSPacker (JNUSPacker) to make the installable, encrypted title."""
    for path, what in ((nuspacker, "NUSPacker.jar"), (key, "the Wii U common key file")):
        if not path.is_file():
            raise SystemExit(
                f"cannot make the installable WUP: {what} not found at {path}. "
                "The title tree in the output folder is the plain Cemu/Loadiine layout; "
                "encrypting it needs NUSPacker and your own common key."
            )
    out.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        ["java", "-jar", str(nuspacker), "-in", str(title_dir), "-out", str(out), "-encryptKeyWith", key.read_text().strip()],
        check=True,
    )


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rpx", type=Path, help="executables/not64-wiiu.rpx")
    ap.add_argument("out", type=Path, help="folder for the title tree")
    ap.add_argument("--name", default="not64 for Wii U")
    ap.add_argument("--publisher", default="not64 Wii U port")
    ap.add_argument("--title-id", default=DEFAULT_TITLE_ID)
    ap.add_argument("--meta-dir", type=Path, default=Path(__file__).resolve().parent.parent / "wiiu" / "meta")
    ap.add_argument("--rom", type=Path, help="your own ROM: copied to the SD files, never into the title")
    ap.add_argument("--autoboot", help="ROM path under sd:/not64 to start straight away")
    ap.add_argument("--sd-dir", type=Path, help="where to write the SD card files")
    ap.add_argument("--nuspacker", type=Path, help="NUSPacker.jar, to also make the encrypted WUP")
    ap.add_argument("--key", type=Path, help="file holding the Wii U common key (yours, not shipped)")
    ap.add_argument("--wup-out", type=Path, help="folder for the encrypted WUP (needs --nuspacker, --key)")
    args = ap.parse_args(argv)

    logging.basicConfig(level=logging.INFO, format="%(message)s")
    build_title(args.rpx, args.out, args.name, args.publisher, args.title_id.lower(), args.meta_dir)
    if args.sd_dir:
        write_sd_files(args.sd_dir, args.rom, args.autoboot)
    if args.nuspacker or args.key:
        if not (args.nuspacker and args.key and args.wup_out):
            ap.error("--nuspacker, --key and --wup-out go together")
        encrypt(args.out, args.wup_out, args.nuspacker, args.key)
    return 0


if __name__ == "__main__":
    sys.exit(main())
