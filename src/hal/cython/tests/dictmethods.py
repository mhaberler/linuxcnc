class PseudoDict:

    def __init__(self, values=None):
        if values is None:
            self.values = []
        else:
            self.values = values

    def __len__(self):
        print "__len__"
        return len(self.values)

    def __getitem__(self, key):
        print "__getitem__"
        # if key is of invalid type or value, the list values will raise the error
        return self.values[key]

    def __contains__(self, key):
        print "__CONTAINS__"

d = PseudoDict()

print 'xxx' in d
