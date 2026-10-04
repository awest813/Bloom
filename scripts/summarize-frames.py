"""Summarize explicitly labeled, low-overhead presentation measurements."""
import argparse
import csv
import json


def summarize(rows):
    if not rows:
        raise ValueError('No intervals selected')
    for index, row in enumerate(rows):
        if row['duration_ms'] < 1000 or row['presentations'] <= 0:
            raise ValueError('Invalid presentation interval')
        if row['sequence'] <= 0 or row['sequence'] % 2:
            raise ValueError('Unfinished sample')
        if index and row['sequence'] != rows[index - 1]['sequence'] + 2:
            raise ValueError('Missing, repeated or reordered interval')
    duration = sum(row['duration_ms'] for row in rows)
    rates = sorted(row['presentations'] * 1000 / row['duration_ms'] for row in rows)
    skipped = rows[-1]['skip_decisions'] - rows[0]['skip_decisions']
    decisions = rows[-1]['total_decisions'] - rows[0]['total_decisions']
    if not 0 <= skipped <= decisions:
        raise ValueError('Scheduling counter reset or overflow')
    return dict(intervals=len(rows), sequence_range=[rows[0]['sequence'], rows[-1]['sequence']],
                emulated_seconds=duration / 1000,
                presentation_fps=sum(row['presentations'] for row in rows) * 1000 / duration,
                guest_vblanks_per_second=sum(row['vblanks'] for row in rows) * 1000 / duration,
                minimum_interval_fps=rates[0],
                intervals_at_least_19_fps=sum(rate >= 19 for rate in rates),
                skip_decision_percent=100 * skipped / decisions if decisions else None)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv')
    parser.add_argument('--first', type=int, required=True)
    parser.add_argument('--last', type=int, required=True)
    parser.add_argument('--scene', required=True)
    args = parser.parse_args()
    with open(args.csv, newline='', encoding='utf-8') as file:
        rows = [{key: int(value) for key, value in row.items()} for row in csv.DictReader(file)]
    result = summarize([row for row in rows if args.first <= row['sequence'] <= args.last])
    result.update(scene=args.scene, source=args.csv)
    print(json.dumps(result, indent=2))
