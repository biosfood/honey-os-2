import gdb

# Define your type map: {StructName: {fieldName: pointeeType}}
type_map = {
    "struct Process": {
        "virtual_memory_entries": "VirtualMemoryEntry",
        "open_file_handles": "FileDescriptor",
        "openFileHandles": "FileDescriptor",
    },
    "struct VirtualMemoryEntry": {
        "mappings": "MemoryMapping",
    },
}

class CustomStructPrinter(gdb.ValuePrinter):
    def __init__(self, val, map):
        self.val = val
        self.type = val.type.strip_typedefs()
        self.map = map

    def to_string(self):
        return f"{self.type.tag or self.type.name}"

    def children(self):
        fields = self.type.fields()
        for field in fields:
            if field.name in self.map:
                elements = []
                current = self.val[field]
                type = gdb.lookup_type(self.map[field.name] + " *")
                while current:
                    elements.append(current['data'])
                    current = current['next']
                expr_items = ', '.join(f'(void*){v}' for v in elements)

                expr = f'({type}[{len(elements)}]){{ {expr_items} }}'
                value = gdb.parse_and_eval(expr)
                yield field.name, value
                # yield field.name + ' - raw', self.val[field]
            else:
                yield field.name, self.val[field]
    def display_hint(self):
        return "struct"


# Dispatcher
def printer_lookup(val):
    typename = str(val.type.strip_typedefs())
    if typename in type_map:
        return CustomStructPrinter(val, type_map[typename])
    return None


# Register globally
gdb.pretty_printers.append(printer_lookup)
