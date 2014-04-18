import ConfigParser
config = ConfigParser.ConfigParser()
config.read('apps.ini')
for s in config.sections():
    for i in config.items(s):
        print i[0],i[1]
