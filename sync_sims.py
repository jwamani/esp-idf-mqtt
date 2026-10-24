import shutil
from pathlib import Path

root = Path(__file__).resolve().parent
build = root / "build"
targets = [root / "sim" / "node2", root / "sim" / "master"]
files = [
    "flasher_args.json",
    "mqtt.bin",
    "bootloader/bootloader.bin",
    "partition_table/partition-table.bin",
]

for target in targets:
    for name in files:
        src = build / name
        dst = target / "build" / name
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
    print(f"synced {target.relative_to(root)}")
