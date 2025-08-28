from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import List, Optional, Tuple


def extract_event_ids(filepath: Path) -> List[int]:
    """Liest eine .json.watch‑Datei und extrahiert die Event‑IDs.

    Bevorzugt das Feld ``event_id``, falls vorhanden; andernfalls wird
    ``packet_id`` verwendet. Es werden nur numerische Werte berücksichtigt.
    """
    ids: List[int] = []
    with filepath.open() as f:
        for line in f:
            try:
                rec = json.loads(line)
            except json.JSONDecodeError:
                continue
            # Wähle Feld: zuerst event_id, dann packet_id
            id_val = rec.get("event_id")
            if id_val is None:
                id_val = rec.get("packet_id")
            if id_val is None:
                continue
            try:
                ids.append(int(id_val))
            except (ValueError, TypeError):
                continue
    return ids


def find_missing_ids(sorted_ids: List[int]) -> Tuple[int, List[str]]:
    """Ermittelt die Anzahl und Bereiche der fehlenden IDs.

    ``sorted_ids`` sollte eine sortierte Liste von IDs sein. Die Funktion
    gibt die Anzahl der fehlenden IDs sowie eine Liste von Zeichenketten
    zurück, die entweder Einzelwerte oder Bereiche in der Form
    "a-b" darstellen.
    """
    missing_count = 0
    missing_ranges: List[str] = []
    if not sorted_ids:
        return missing_count, missing_ranges
    # Sortiere sicherheitshalber
    sorted_ids.sort()
    last = sorted_ids[0]
    for current in sorted_ids[1:]:
        diff = current - last
        if diff > 1:
            # Es fehlen (diff-1) IDs zwischen last und current
            missing_count += diff - 1
            if diff == 2:
                # Genau eine fehlende ID
                missing_ranges.append(str(last + 1))
            else:
                # Bereich fehlender IDs
                missing_ranges.append(f"{last + 1}-{current - 1}")
        last = current
    return missing_count, missing_ranges


def analyze_file(filepath: Path) -> Optional[Tuple[int, int, List[str]]]:
    """Analysiert eine einzelne .json.watch‑Datei.

    Gibt ein Tupel mit (gesamtzahl, fehlende_anzahl, fehlende_ranges) zurück.
    Bei leeren oder nicht parsebaren Dateien wird ``None`` zurückgegeben.
    """
    ids = extract_event_ids(filepath)
    if not ids:
        return None
    missing_count, missing_ranges = find_missing_ids(ids)
    return len(ids), missing_count, missing_ranges


def main(argv: List[str]) -> None:
    basedir = Path(argv[0]) if argv else Path.cwd()
    if not basedir.exists():
        print(f"Fehler: Basisverzeichnis {basedir} existiert nicht.", file=sys.stderr)
        sys.exit(1)
    watch_files = list(basedir.rglob("*.json.watch"))
    if not watch_files:
        print(f"Keine .json.watch‑Dateien unter {basedir} gefunden.")
        return
    for wf in sorted(watch_files):
        result = analyze_file(wf)
        if result is None:
            print(f"{wf}: keine gültigen Event‑IDs gefunden.")
        else:
            total, missing, ranges = result
            if missing == 0:
                print(f"{wf}: {total} Events, keine fehlenden IDs.")
            else:
                range_str = ", ".join(ranges)
                print(f"{wf}: {total} Events, {missing} fehlende ID(s):")


if __name__ == "__main__":
    main(sys.argv[1:])