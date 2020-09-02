import subprocess


class KCtlPrintFormat(object):
    @classmethod
    def to_optstr(cls): return cls.option

    @classmethod
    def to_name(cls): return cls.name

    def __eq__(self, format_str):
        return self.__class__.name == format_str

class HexFormat(KCtlPrintFormat):
    name   = 'hex'
    option = '-X'


class CharFormat(KCtlPrintFormat):
    name   = 'chars'
    option = '-A'


class PrintFormatFactory(object):
    char_format = CharFormat()
    hex_format  = HexFormat()

    @classmethod
    def to_optstr(cls, print_format):
        if   print_format == cls.char_format: return cls.char_format.to_optstr()
        elif print_format == cls.hex_format:  return cls.hex_format.to_optstr()

        # for string output (default)
        return ''

    @classmethod
    def from_namestr(cls, print_format):
        if   print_format == cls.char_format: return cls.char_format
        elif print_format == cls.hex_format:  return cls.hex_format


class KCtl(object):
    """
    This is a wrapper around the `kctl` kinetic command-line tool.
    """

    prog_bin = '/home/akmontan/code/kinetic-prototype/toolbox/kctl/kctl'

    commands = {
        'get'    : 'Get key value',
        'getnext': 'Get next key value',
        'getprev': 'Get previous key value',
        'getvers': 'Get version of key value entry',
        'put'    : 'Insert or update key value',
        'del'    : 'Delete key value',
        'info'   : 'Get device information',
        'range'  : 'Get a range of keys',
    }

    @classmethod
    def test_get(cls, key_name, key_format='chars'):
        return KCtlGet(key_format=key_format).run(key_name, capture_output=True)

    @classmethod
    def test_getcmd(cls, key_format='chars'):
        return KCtlGet(key_format=key_format)

    def __init__(self, host='localhost', port=8123, **kwargs):
        super().__init__(**kwargs)

        self.host = host
        self.port = port


class KCtlGet(KCtl):
    """
    This is a class for the get command of the `kctl` kinetic command-line tool.
    """

    def __init__(self, key_format='chars', **kwargs):
        super().__init__(**kwargs)

        self.key_format = PrintFormatFactory.from_namestr(key_format)

    def run(self, key_name, capture_output=False):
        run_args = [
            self.__class__.prog_bin    ,
            '-h'                       ,
            self.host                  ,
            '-p'                       ,
            str(self.port)             ,
            'get'                      ,
            self.key_format.to_optstr(),
            key_name                   ,
        ]

        if not capture_output:
            return subprocess.run(
                run_args,
                stderr=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                encoding='utf-8'
            )

        return subprocess.run(
            run_args,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            encoding='utf-8'
        )

if __name__ == '__main__':
    kctl_proc = KCtl.test_get('pak')
    print(kctl_proc.stdout)
    print(kctl_proc.stderr)
