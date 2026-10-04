"""Generate an isolated SH-4 MAC.W experiment; production GTE is unchanged."""
import argparse
from pathlib import Path
from benchmark_gte_bounds import source as bounds_source
from test_regressions import extract_function


def source():
    text = bounds_source(production=True).replace('}static inline', '}\nstatic inline')
    reference = extract_function(text, 'reference_bounds')
    current = extract_function(text, 'candidate_bounds')
    text = text.replace(reference, current.replace('candidate_bounds', 'reference_bounds'))
    before, candidate = text.split('#define BOUNDS_ candidate_bounds\n', 1)
    helper = r'''
/* SR.S=0 selects the full 64-bit accumulator (SH-4 manual section 9.55).
 * Preserve SR: use the portable calculation if the caller enables saturation.
 * KOS interrupt/thread entry saves and restores MACH/MACL. */
static inline s64 dot3(const s16 *matrix, const s16 *vector, s32 translation) {
#ifdef __sh__
    u32 lo = (u32)translation << 12, hi = (u32)(translation >> 20);
    {
        const s16 *m = matrix, *v = vector;
        __asm__ volatile (
            "lds %0,macl\n\t"
            "lds %1,mach\n\t"
            "mac.w @%2+,@%3+\n\t"
            "mac.w @%2+,@%3+\n\t"
            "mac.w @%2+,@%3+\n\t"
            "sts macl,%0\n\t"
            "sts mach,%1"
            : "+r"(lo), "+r"(hi), "+&r"(m), "+&r"(v)
            : "m"(matrix[0]), "m"(matrix[1]), "m"(matrix[2]),
              "m"(vector[0]), "m"(vector[1]), "m"(vector[2])
            : "mach", "macl");
        return (s64)(s32)hi * 4294967296LL + lo;
    }
#endif
    return (s64)translation*4096 + (s64)matrix[0]*vector[0] + (s64)matrix[1]*vector[1] +
           (s64)matrix[2]*vector[2];
}
'''
    for name, prefix, base, triple in [('RTPS','R',0,False),('RTPT','R',0,True),
                                      ('NCCS','L',16,False),('NCCT','L',16,True)]:
        old = extract_function(candidate, 'gte' + name)
        new = old
        xyz = ('vx','vy','vz') if triple else ('gteVX0','gteVY0','gteVZ0')
        vector = '(s16 *)&regs->CP2D.p[v << 1]' if triple else '(s16 *)&regs->CP2D.p[0]'
        for row in range(1,4):
            terms = [f'(gte{prefix}{row}{col} * {xyz[col-1]})' for col in range(1,4)]
            expr = ' + '.join(terms)
            translation = 'gteTR' + 'XYZ'[row-1] if prefix == 'R' else '0'
            replacement = f'dot3((s16 *)&regs->CP2C.p[0] + {base+(row-1)*3}, {vector}, {translation})'
            if prefix == 'L':
                expr = '(s64)' + expr
            else:
                expr = f'((s64){translation} << 12) + ' + expr
            assert expr in new, (name, expr)
            new = new.replace(expr, replacement)
        if triple:
            new = new.replace('\ts32 vx, vy, vz;\n', '')
            for axis in ('x', 'y', 'z'):
                new = new.replace(f'\t\tv{axis} = V{axis.upper()}(v);\n', '')
        pos = new.index('{') + 1
        new = new[:pos] + f'''
#ifdef __sh__
    u32 sr; __asm__ volatile ("stc sr,%0" : "=r"(sr));
    if (sr & 2) {{ reference_{name}(regs); return; }}
#endif
''' + new[pos:]
        candidate = candidate.replace(old,new)
    text = before + '#define BOUNDS_ candidate_bounds\n' + helper + candidate
    text = text.replace('uint64_t a = 0, b = 0;',
        'if (op != 0 && op != 9 && op != 18 && op != 21) continue;\n        uint64_t a = 0, b = 0;')
    text = text.replace('    check();', r'''
    check();
#ifdef __sh__
    __asm__ volatile ("sets");
    check();
    u32 sr_after; __asm__("stc sr,%0" : "=r"(sr_after));
    assert(sr_after & 2);
    __asm__ volatile ("clrs");
#endif
''')
    return text


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emit', type=Path, required=True)
    args = parser.parse_args()
    args.emit.write_text(source())
