#!/bin/bash

# 选择性烧录脚本 - 只烧录修改过的二进制文件

# 构建目录
BUILD_DIR="./build"

# 二进制文件配置 (文件路径:烧录地址)
BIN_FILES=(
    "bootloader/bootloader.bin:0x0"
    "xiaozhi.bin:0x20000"
    "partition_table/partition-table.bin:0x8000"
    "ota_data_initial.bin:0xd000"
    "generated_assets.bin:0x800000"
)

# 记录文件修改时间的文件
TIMESTAMP_FILE="./flash_timestamps.txt"

# 确保构建目录存在
if [ ! -d "$BUILD_DIR" ]; then
    echo "错误：构建目录不存在，请先运行 idf.py build"
    exit 1
fi

# 生成有效的变量名
function generate_var_name() {
    local file_path=$1
    # 将非字母数字字符替换为下划线
    echo "$file_path" | sed 's/[^a-zA-Z0-9]/_/g'
}

# 读取上次烧录的时间戳
if [ -f "$TIMESTAMP_FILE" ]; then
    source "$TIMESTAMP_FILE"
else
    # 初始化时间戳
    for bin_config in "${BIN_FILES[@]}"; do
        bin_file=$(echo "$bin_config" | cut -d: -f1)
        var_name=$(generate_var_name "$bin_file")
        declare "last_${var_name}_timestamp=0"
    done
fi

# 确定需要烧录的文件
FILES_TO_FLASH=()

for bin_config in "${BIN_FILES[@]}"; do
    bin_file=$(echo "$bin_config" | cut -d: -f1)
    bin_addr=$(echo "$bin_config" | cut -d: -f2)
    bin_path="$BUILD_DIR/$bin_file"

    if [ -f "$bin_path" ]; then
        current_timestamp=$(stat -c %Y "$bin_path")
        var_name=$(generate_var_name "$bin_file")
        last_timestamp_var="last_${var_name}_timestamp"
        last_timestamp=${!last_timestamp_var:-0}

        if [ $current_timestamp -gt $last_timestamp ]; then
            echo "需要烧录: $bin_file (时间戳: $current_timestamp > $last_timestamp)"
            FILES_TO_FLASH+=("$bin_path $bin_addr")
            # 更新时间戳
            declare "last_${var_name}_timestamp=$current_timestamp"
        else
            echo "跳过: $bin_file (未修改)"
        fi
    else
        echo "警告: $bin_file 不存在"
    fi
done

# 烧录需要更新的文件
if [ ${#FILES_TO_FLASH[@]} -gt 0 ]; then
    echo "开始烧录..."

    # 构建烧录参数
    FLASH_ARGS=""
    for file_addr in "${FILES_TO_FLASH[@]}"; do
        FLASH_ARGS+=" $file_addr"
    done

    # 使用 idf.py flash 命令，只烧录指定的文件
    echo "执行命令: idf.py flash --flash-args \"$FLASH_ARGS\""
    idf.py flash --flash-args "$FLASH_ARGS"

    if [ $? -eq 0 ]; then
        echo "烧录成功！"
        # 保存时间戳
        > "$TIMESTAMP_FILE"  # 清空文件
        for bin_config in "${BIN_FILES[@]}"; do
            bin_file=$(echo "$bin_config" | cut -d: -f1)
            var_name=$(generate_var_name "$bin_file")
            timestamp_var="last_${var_name}_timestamp"
            timestamp=${!timestamp_var:-0}
            echo "${timestamp_var}=$timestamp" >> "$TIMESTAMP_FILE"
        done
    else
        echo "烧录失败！"
        exit 1
    fi
else
    echo "没有需要烧录的文件，所有文件都是最新的。"
fi
