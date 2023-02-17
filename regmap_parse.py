#! /usr/bin/env python
# -*- coding: utf-8 -*-

import sys
import re

with open("include/regmap-structs.h") as f:
    regmap_file = f.read()

regions = []

types = []
names = []
rws = []
type_desc = []

for r in re.finditer(r"m\((.*),\s*(\S*),\s*(\S*)\)", regmap_file):
    types.append(r.group(1))
    names.append(r.group(2))
    rws.append(r.group(3))

regaddr = 0
markdown = ""
markdown += "| Reg Hex | Reg Dec | Group | Name | Data |||||||| Comment\n"
markdown += "| ^^ | ^^ | ^^ | ^^ | bit 7 | bit 6 | bit 5 | bit 4 | bit 3 | bit 2 | bit 1 | bit 0 | ^^ |\n"

c_regmap = ""

for t, n, rw in zip(types, names, rws):
    print("Region {} (rw:{})".format(n, rw))
    rw_str = "RO"
    if rw:
        rw_str = "RW"
    c_regmap += "\n"
    c_regmap += "/* Region {}: {} */\n".format(n, rw_str)

    s = str(t)
    desc = []
    if s.startswith("struct"):
        ex = s.replace("struct", "__REGMAP_STRUCT") + " {\\n((.*;\n)*)};\\n"
        st = re.search(ex, regmap_file)
        vars = st.group(1).split(";\n")
        vars.pop()
        bit_sequence = False
        bit_cnt = 0
        for v in vars:
            # bit or not
            v_name = v.strip().split().pop().split(":")[0].upper()
            if v.find(":") != -1:
                bit_sequence = True
                if bit_cnt == 8:
                    bit_cnt = 0
                    regaddr += 1
                bits = int(v.split(":").pop())
                print("  {}: {} BIT {}".format(regaddr, bits, v))
                if bit_cnt == 0:
                    c_regmap += "#define WBEC_REG_{}_{}\t{}\n".format(n, regaddr, regaddr).expandtabs(60)

                c_regmap += "   #define WBEC_REG_{}_{}_{}_MSK\tGENMASK({}, {})\n".format(n, regaddr, v_name, bit_cnt + bits, bits).expandtabs(60)

                bit_cnt += bits
            else:
                if bit_sequence:
                    bit_sequence = False
                    regaddr += 1
                if v.find("int16_t") != -1:
                    assert regaddr % 2 == 0, "Reg address must be even for 16 bit regs"
                    print("  {}-{}: {}".format(regaddr, regaddr + 1, v))
                    c_regmap += "#define WBEC_REG_{}_{}\t{}\n".format(n, v_name, regaddr).expandtabs(60)
                    regaddr += 2
                elif v.find("int8_t") != -1:
                    print("  {}: {}".format(regaddr, v))
                    c_regmap += "#define WBEC_REG_{}_{}\t{}\n".format(n, v_name, regaddr).expandtabs(60)
                    regaddr += 1
                else:
                    raise "Not support type"

        if bit_sequence:
            bit_sequence = False
            regaddr += 1

with open("regmap.md", "w") as md:
    md.write(markdown)

with open("c_regmap.h", "w") as c:
    c.write(c_regmap)
