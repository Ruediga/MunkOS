import subprocess
import sys

if len(sys.argv) == 2:
    outfile = sys.argv[1]
else:
    print("debug: args provided -> ")
    for arg in sys.argv:
        print(arg)
    print("syntax: python prepsymboltable.py outputfile")
    exit(1)

res = subprocess.run(['nm', './bin/kernel.elf_x86_64', '-n'], text=True, stdout=open(outfile, 'w'))

if res.returncode:
    print("Failed to run nm: ", res.stderr)
    exit(1)

with open(outfile, "r") as file:
    out_map = "#include \"stacktrace.h\"\n\nstruct stacktrace_symbol_table_entry stacktrace_symtable[] = {\n"
    for line in file:
        words_per_line = line.split()
        
        if words_per_line[1] in ("T", "t"):
            out_map += "    {0x" + words_per_line[0] + ", \"" + words_per_line[2] + "\"},\n"
    out_map += "    {0x0, \"INVALID SYMBOL\"}\n};"

with open(outfile, "w") as file:
    file.write(out_map)