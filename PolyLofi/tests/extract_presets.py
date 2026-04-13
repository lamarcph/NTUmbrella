import json, sys
with open('Lofior.json') as f:
    data = json.load(f)
for slot in data['slots']:
    if slot['guid'] == 'Poly':
        for preset in slot.get('presets', []):
            name = preset.get('name', '')
            if name in ['Fizzy Keys', 'Sync Lead', 'Crushed']:
                print(f'=== {name} ===')
                for k, v in preset.get('params', {}).items():
                    print(f'  {k}: {v}')
                print()
