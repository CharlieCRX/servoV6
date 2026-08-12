// ============================================================================
// AppVnextAnchor.cpp —— application_vnext 库锚点
// ============================================================================
// 保持 STATIC 库至少有一个编译单元，保证链接稳定（与 domain_vnext 同套路）。
// ============================================================================
namespace application_vnext {
[[maybe_unused]] int appVnextAnchor() { return 0; }
}  // namespace application_vnext
