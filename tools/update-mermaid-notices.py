#!/usr/bin/env python3
"""Refresh redistributable license texts for the locked Mermaid dependency graph."""

import json
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parent.parent
manifest = root / "third_party/mermaid_bridge/Cargo.toml"
metadata = json.loads(subprocess.check_output(
    ["cargo", "metadata", "--locked", "--format-version", "1", "--manifest-path", str(manifest)],
    text=True,
))
sections = [
    "Native Mermaid renderer: third-party notices\n"
    "Generated from third_party/mermaid_bridge/Cargo.lock.\n"
    "Refresh with: python tools/update-mermaid-notices.py\n"
    "The bridge disables mermaid-rs-renderer's CLI and PNG features.\n"
]
for package in sorted(metadata["packages"], key=lambda package: package["name"]):
    if package["source"] is None:
        continue
    directory = Path(package["manifest_path"]).parent
    licenses = sorted(path for path in directory.iterdir() if path.is_file()
                      and path.name.upper().startswith(("LICENSE", "COPYING", "COPYRIGHT")))
    license_directory = directory / "licenses"
    if license_directory.is_dir():
        licenses.extend(sorted(path for path in license_directory.rglob("*") if path.is_file()))
    if not licenses:
        raise RuntimeError(f"No license text found for {package['name']} in {directory}")
    sections.append(f"\n{'=' * 72}\n{package['name']} {package['version']}\n"
                    f"License: {package['license']}\n"
                    f"Source: {package.get('repository') or 'https://crates.io/crates/' + package['name']}\n")
    for path in licenses:
        sections.append(f"\n--- {path.relative_to(directory)} ---\n{path.read_text(encoding='utf-8')}\n")
output = manifest.parent / "THIRD_PARTY_NOTICES"
output.write_text("\n".join(sections).rstrip() + "\n", encoding="utf-8", newline="\n")
print(f"Wrote {output.relative_to(root)} ({len(metadata['packages']) - 1} packages)")
sysroot = Path(subprocess.check_output(["rustc", "--print", "sysroot"], text=True).strip())
rust_notices = (sysroot / "share/doc/rust/COPYRIGHT-library.html").read_text(encoding="utf-8")
(manifest.parent / "RUST_LIBRARY_NOTICES.html").write_text(
    "\n".join(line.rstrip() for line in rust_notices.splitlines()) + "\n", encoding="utf-8", newline="\n")
