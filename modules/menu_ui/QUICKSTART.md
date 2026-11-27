# A7C2 Menu UI - 快速开始指南

## 状态概览

```
✅ 代码移植完成
✅ 编译系统配置完成  
✅ 模块身份验证配置完成
⚠️  appFw偏移量待更新 (使用a6700占位符)
❌ 未在实际相机测试
```

**当前状态**: 不可用于生产环境，需要完成偏移量更新

## 文件结构

```
Pilot/modules/menu_ui/
├── 📄 核心代码
│   ├── pilot_menu_ui.c          主模块 (2472行, 包含所有APPFW偏移)
│   ├── pilot_ui.h               公共API头文件
│   └── *.inc                    功能模块 (UI/导航/选项/忙碌对话框)
│
├── 🔧 构建工具
│   ├── build.sh                 构建脚本 (支持a7c2)
│   └── verify_offsets.sh        偏移验证工具
│
├── 📋 配置文件
│   ├── device.json              目标设备: a7c2
│   └── config.txt               模块元数据
│
└── 📚 文档
    ├── PORTING_SUMMARY.md       移植总结 (本文档的详细版)
    ├── README.a7c2.md           A7C2移植说明
    ├── TODO.a7c2.md             详细待办清单 (90+项)
    └── QUICKSTART.md            本文档
```

## 3步走战略

### Step 1: 获取和分析 appFw.so

```bash
# 1. 从a7c2固件提取 appFw.so
# 位置: 固件包中 p15:/lib/appFw.so 或相机 /usr/lib/appFw.so

# 2. 验证文件
file appFw.so
# 应输出: ELF 64-bit LSB shared object, ARM aarch64, ...

# 3. 运行初步验证
./verify_offsets.sh /path/to/appFw.so
```

### Step 2: 逆向工程确定偏移

使用 IDA Pro 或 Ghidra:

```
1. 加载 appFw.so (AArch64 ELF)
2. 定位关键区域:
   - .got.plt 段 (GOT表)
   - 菜单相关字符串
   - 屏幕工厂函数
3. 对比 a6700 实现找到对应位置
4. 记录所有偏移量
```

**关键优先级** (按重要性排序):

1. **GOT表地址** (8个)
   - APPFW_GOT_MENU_TABLE
   - APPFW_GOT_PAGE_TABLE  
   - APPFW_GOT_LAYOUT_1C3
   - APPFW_GOT_LAYOUT_293
   - 其他GOT指针...

2. **数据结构偏移** (5个)
   - APPFW_MENU_TABLE_OFF
   - APPFW_CUSTOM_GROUP_OFF
   - APPFW_PAGE_TABLE_OFF
   - APPFW_LAYOUT_1C3_OFF
   - APPFW_LAYOUT_293_OFF

3. **函数偏移** (10+个)
   - 工厂函数
   - 按钮处理器
   - 辅助函数

详细清单见 `TODO.a7c2.md`

### Step 3: 更新和验证

```bash
# 1. 编辑 pilot_menu_ui.c
vim pilot_menu_ui.c
# 搜索 "ILCE-7CM2" 注释块，更新下面的所有 #define

# 2. 验证更新
./verify_offsets.sh
# 应显示: ✓ 无a6700占位符

# 3. 编译
export PILOT_MODEL=a7c2
export PILOT_A7C2_SYSROOT=/path/to/sysroot
./build.sh

# 4. 检查生成的模块
readelf -s ../../build/pilot_menu_ui.so | grep pilot_module_model
# 应找到: pilot_module_model_a7c2
```

## 编译选项

### 环境变量

```bash
PILOT_MODEL=a7c2              # 目标机型 (必需)
PILOT_A7C2_SYSROOT=/path      # a7c2 sysroot路径
AARCH64_CC=aarch64-linux-gnu-gcc-11  # 交叉编译器
```

### 编译命令

```bash
# 基础编译
cd Pilot/modules/menu_ui
export PILOT_MODEL=a7c2
./build.sh

# 指定输出路径
./build.sh /path/to/output/pilot_menu_ui.so

# 完整示例
PILOT_MODEL=a7c2 \
PILOT_A7C2_SYSROOT=/opt/a7c2-sysroot \
AARCH64_CC=aarch64-linux-gnu-gcc-11 \
./build.sh ../../build/pilot_menu_ui.so
```

## 安装流程

**⚠️ 警告**: 仅在偏移量验证完成后执行！

```bash
# 1. 准备SD卡
mkdir -p /sdcard/pilot/modules/menu_ui

# 2. 复制模块
cp pilot_menu_ui.so /sdcard/pilot/

# 3. 创建/更新 modules.json
cat > /sdcard/pilot/modules.json << 'EOF'
{
  "modules": {
    "pilot_menu_ui.so": true
  }
}
EOF

# 4. (可选) 添加配置
cp config.txt /sdcard/pilot/modules/menu_ui/

# 5. 安全重启相机
# - 完整关机 (不是休眠)
# - 等待3秒
# - 开机
```

## 测试检查清单

测试前准备:
- [ ] 有固件备份
- [ ] 有恢复方案 (USB刷写)
- [ ] 可以通过SSH访问 (推荐)
- [ ] 阅读了所有警告

基础功能测试:
- [ ] 相机正常启动
- [ ] "自定义设置"菜单中出现"Pilot"项
- [ ] 可以进入Pilot菜单
- [ ] 显示版本信息
- [ ] 显示模块列表
- [ ] 可以查看模块详情
- [ ] 开关切换工作正常
- [ ] 可以正常退出到主菜单

## 故障排查

### 编译失败

```bash
# 检查工具链
which aarch64-linux-gnu-gcc-11
# 检查sysroot
ls $PILOT_A7C2_SYSROOT/lib/libc.so.6
# 检查模型宏
grep PILOT_DECLARE_MODULE_MODEL pilot_menu_ui.c
```

### 相机崩溃

如果安装后相机崩溃:
1. 移除SD卡
2. 重启相机
3. 检查日志 `/pilot/log.txt`
4. 验证偏移量是否正确

### 模块不加载

检查:
```bash
# 1. 模块标识
readelf -s pilot_menu_ui.so | grep pilot_module_model_a7c2

# 2. 依赖库
readelf -d pilot_menu_ui.so | grep NEEDED

# 3. Pilot日志
cat /sdcard/pilot/log.txt
```

## 关键文件位置

在相机上:
```
/usr/lib/pilot_a7c2.so          Pilot框架DSO
/usr/lib/appFw.so               应用固件 (需要分析)
/pilot/pilot_menu_ui.so         本模块
/pilot/modules.json             模块清单
/pilot/log.txt                  Pilot日志
/tmp_wbshd/pilot-a7c2-boot.bin  启动追踪
```

在开发机:
```
Pilot/modules/menu_ui/          模块源码
Pilot/build/pilot_menu_ui.so    编译输出
Pilot/patch.a7c2.json           A7C2补丁描述
Pilot/models.json               机型配置
```

## 获取帮助

1. **查看详细文档**
   - `PORTING_SUMMARY.md` - 完整移植总结
   - `TODO.a7c2.md` - 详细待办清单
   - `README.a7c2.md` - 技术说明

2. **参考实现**
   - `Himpq-Pilot/modules/menu_ui/` - A6700原始实现
   - 对比代码理解工作原理

3. **检查状态**
   ```bash
   ./verify_offsets.sh
   ```

## 下一步

根据你的目标:

**如果你要更新偏移量**:
→ 阅读 `TODO.a7c2.md` 第1节
→ 准备IDA Pro/Ghidra
→ 获取appFw.so
→ 开始分析

**如果你要测试编译**:
→ 配置环境变量
→ 运行 `./build.sh`
→ 检查输出

**如果你要理解代码**:
→ 阅读 `PORTING_SUMMARY.md` 技术架构部分
→ 对比 Himpq-Pilot 实现
→ 查看 `pilot_ui.h` API文档

---

**记住**: 在偏移量验证之前，这个模块**不能**在实际相机上使用！
