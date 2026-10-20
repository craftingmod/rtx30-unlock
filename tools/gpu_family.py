"""GA10x chip identity, independent of board vendor or firmware filenames."""

CHIPS = {0x172: 'GA102', 0x173: 'GA103', 0x174: 'GA104',
         0x176: 'GA106', 0x177: 'GA107'}


def pci_candidate(identity):
    return identity & 0xffff == 0x10de and identity >> 24 in (0x22, 0x24, 0x25)


def chip_name(boot0):
    return CHIPS.get((boot0 >> 20) & 0x1ff, 'unknown')
