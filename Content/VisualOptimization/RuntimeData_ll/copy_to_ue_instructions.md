# D6G-A2 Copy Instructions

Copy the contents of this RuntimeData package into the UE project RuntimeData directory.

Source package:
`I:\Disertation\VisualOptimise\outputs\runs\20260812_230203_test_map1_clean_d6g_a2_material_manifest_runtime_export\03_runtime_data_package`

UE destination:
`I:\Disertation\VisualOptimizationUE\Content\VisualOptimization\RuntimeData`

PowerShell example:

```powershell
Copy-Item -Path "I:\Disertation\VisualOptimise\outputs\runs\20260812_230203_test_map1_clean_d6g_a2_material_manifest_runtime_export\03_runtime_data_package\*" -Destination "I:\Disertation\VisualOptimizationUE\Content\VisualOptimization\RuntimeData" -Recurse -Force
```

Current UE compatibility path reads `materials/textures/<material_slot_id>/basecolor.png`.
D6G-A2 also packages `materials/backend_candidates/` for future backend switching, but current UE code may ignore it until extended.
