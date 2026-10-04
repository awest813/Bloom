"""Summarize completed, explicitly selected register-I/O diagnostic intervals."""
import argparse
import collections
import csv
import json

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('csv')
parser.add_argument('--first', type=int, required=True)
parser.add_argument('--last', type=int, required=True)
parser.add_argument('--scene', required=True)
args = parser.parse_args()
with open(args.csv, newline='', encoding='utf-8') as source:
    rows = [row for row in csv.DictReader(source)
            if args.first <= int(row['sequence']) <= args.last]
intervals = {}
totals = collections.defaultdict(lambda: [0, 0])
for row in rows:
    sequence, duration = int(row['sequence']), int(row['duration_us'])
    if sequence % 2 or duration < 5000000 or duration > 60000000:
        raise ValueError('Invalid diagnostic interval')
    if sequence in intervals and intervals[sequence] != duration:
        raise ValueError('Inconsistent interval duration')
    intervals[sequence] = duration
    entry = totals[(row['kind'], row['offset'], int(row.get('mode', 0))) ]
    entry[0] += int(row['calls'])
    entry[1] += int(row['usec'])
sequences = sorted(intervals)
if sequences != list(range(args.first, args.last + 1, 2)):
    raise ValueError('Missing or incomplete diagnostic interval')
duration = sum(intervals.values())
print(json.dumps(dict(
    scene=args.scene, source=args.csv, sequence_range=[args.first, args.last],
    emulated_seconds=duration / 1000000,
    io_percent=100 * sum(value[1] for value in totals.values()) / duration,
    entries=[dict(kind=key[0], offset=key[1], mode=key[2], calls=value[0], usec=value[1],
                  percent=100 * value[1] / duration)
             for key, value in sorted(totals.items(), key=lambda pair: -pair[1][1])]
), indent=2))
