import struct
import unittest
from build_database import parse_dbc


def dbc(fields, row, strings=b'\0Name\0'):
    return b'WDBC' + struct.pack('<4I', 1, fields, fields * 4, len(strings)) + struct.pack(f'<{fields}I', *row) + strings


class BuilderTests(unittest.TestCase):
    def test_area_layout(self):
        row = [0] * 36
        row[0], row[1], row[2], row[4], row[11] = 87, 0, 12, 65, 1
        self.assertEqual(parse_dbc(dbc(36, row), 'AreaTable')[0], dict(areaId=87, mapId=0, parentAreaId=12, flags=65, name='Name'))

    def test_map_layout(self):
        row = [0] * 66
        row[5] = 1
        self.assertEqual(parse_dbc(dbc(66, row), 'Map')[0], dict(mapId=0, name='Name'))

    def test_bad_offset(self):
        row = [0] * 36
        row[11] = 99
        with self.assertRaises(ValueError):
            parse_dbc(dbc(36, row), 'AreaTable')

    def test_bad_length(self):
        with self.assertRaises(ValueError):
            parse_dbc(dbc(36, [0] * 36)[:-1], 'AreaTable')

    def test_unsupported_layout(self):
        with self.assertRaises(ValueError):
            parse_dbc(dbc(126, [0] * 126), 'Map')


if __name__ == '__main__':
    unittest.main()
