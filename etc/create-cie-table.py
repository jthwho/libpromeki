#!/usr/bin/env python
import csv
import sys

print("// CIE 1931 Wavelength Table")
print("#pragma once")
print()
print("#include <promeki/ciepoint.h>")
print()
print("namespace promeki {");
print()
print("struct CIEWavelength {")
print("\tdouble        wavelength;")
print("\tCIEPoint::XYZ xyz;")
print("\tCIEPoint      xy;")
print("};")
print()
print("static const CIEWavelength cieWavelengthTable[] = {")
with open('CIE_cc_1931_2deg.csv', 'r') as file:
    reader = csv.reader(file);
    for line in reader:
        wavelength = float(line[0])
        x = float(line[1])
        y = float(line[2])
        z = float(line[3])
        x_norm = x / (x + y + z)
        y_norm = y / (x + y + z)

        print("\t{{ {:.1f}, {{ {:.6f}, {:.6f}, {:.6f} }}, {{ {:.6f}, {:.6f} }} }},".format(wavelength, x, y, z, x_norm, y_norm))

print("};")
print()
print("} // namespace promeki");
