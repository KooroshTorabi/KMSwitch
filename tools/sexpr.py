"""Minimal S-expression parser/writer for KiCad files."""
import re

class Sym(str):
    """Unquoted atom."""
    pass

_tok = re.compile(r'\s*(?:(\()|(\))|"((?:[^"\\]|\\.)*)"|([^\s()"]+))', re.S)

def parse(text):
    stack = [[]]
    pos = 0
    n = len(text)
    while pos < n:
        m = _tok.match(text, pos)
        if not m:
            if text[pos:].strip() == '':
                break
            raise ValueError('parse error at %d: %r' % (pos, text[pos:pos+40]))
        pos = m.end()
        if m.group(1):
            stack.append([])
        elif m.group(2):
            x = stack.pop()
            stack[-1].append(x)
        elif m.group(3) is not None:
            stack[-1].append(m.group(3).replace('\\"', '"').replace('\\\\', '\\'))
        else:
            stack[-1].append(Sym(m.group(4)))
    return stack[0][0] if len(stack[0]) == 1 else stack[0]

def q(s):
    return '"' + str(s).replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n') + '"'

def fmt_num(v):
    if isinstance(v, float):
        s = ('%.4f' % v).rstrip('0').rstrip('.')
        return '0' if s in ('-0', '') else s
    return str(v)

def dump(x, indent=0):
    if isinstance(x, list):
        if not x:
            return '()'
        # short lists on one line
        flat = all(not isinstance(e, list) for e in x)
        if flat:
            return '(' + ' '.join(atom(e) for e in x) + ')'
        parts = []
        head = []
        i = 0
        while i < len(x) and not isinstance(x[i], list):
            head.append(atom(x[i])); i += 1
        s = '(' + ' '.join(head)
        for e in x[i:]:
            s += '\n' + '  ' * (indent + 1) + dump(e, indent + 1)
        s += '\n' + '  ' * indent + ')'
        return s
    return atom(x)

def atom(e):
    if isinstance(e, Sym):
        return str(e)
    if isinstance(e, bool):
        return 'yes' if e else 'no'
    if isinstance(e, (int, float)):
        return fmt_num(e)
    return q(e)

def find(lst, key):
    for e in lst:
        if isinstance(e, list) and e and e[0] == key:
            return e
    return None

def findall(lst, key):
    return [e for e in lst if isinstance(e, list) and e and e[0] == key]
