def compare_text_tables(file1, file2):
    with open(file1, 'r') as f1, open(file2, 'r') as f2:
        lines1 = f1.readlines()
        lines2 = f2.readlines()

    if len(lines1) != len(lines2):
        print(f"行数不同: {file1} 有 {len(lines1)} 行, {file2} 有 {len(lines2)} 行")
        return False

    for i, (line1, line2) in enumerate(zip(lines1, lines2)):
        if line1 != line2:
            print(f"第 {i+1} 行不一致:")
            print(f"{file1}: {line1.strip()}")
            print(f"{file2}: {line2.strip()}")
            return False

    print("两个文本格式的CRC64表完全相同！")
    return True

def compare_binary_tables(file1, file2):
    with open(file1, 'rb') as f1, open(file2, 'rb') as f2:
        data1 = f1.read()
        data2 = f2.read()

    if len(data1) != len(data2):
        print(f"文件大小不同: {file1} 有 {len(data1)} 字节, {file2} 有 {len(data2)} 字节")
        return False

    if data1 != data2:
        print("二进制内容不同！")
        # 找到第一个不同的字节位置
        for i, (b1, b2) in enumerate(zip(data1, data2)):
            if b1 != b2:
                print(f"第一个差异在偏移 {i} 处: {hex(b1)} != {hex(b2)}")
                return False

    print("两个二进制格式的CRC64表完全相同！")
    return True

def compare_crc64_tables(file1, file2):
    # 尝试检测文件格式（通过文件扩展名或内容）
    is_binary1 = file1.endswith('.bin')
    is_binary2 = file2.endswith('.bin')

    if is_binary1 != is_binary2:
        print("错误：一个文件是二进制格式，另一个是文本格式")
        return False

    if is_binary1:
        return compare_binary_tables(file1, file2)
    else:
        return compare_text_tables(file1, file2)

if __name__ == "__main__":
    import sys
    if len(sys.argv) != 3:
        print("用法: python compare_crc64.py <文件1> <文件2>")
        sys.exit(1)

    file1, file2 = sys.argv[1], sys.argv[2]
    compare_crc64_tables(file1, file2)