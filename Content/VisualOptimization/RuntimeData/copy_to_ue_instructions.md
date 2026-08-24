# RuntimeData Copy Instructions

Copy the contents of this RuntimeData package into the UE project RuntimeData directory.

Source package:
`I:\Disertation\VisualOptimise\outputs\runs\20260823_204504_deep_shrine_vault_level_runtime_export\03_runtime_data_package`

UE destination:
`I:\Disertation\VisualOptimizationUE\Content\VisualOptimization\RuntimeData`

PowerShell example:

```powershell
Copy-Item -Path "I:\Disertation\VisualOptimise\outputs\runs\20260823_204504_deep_shrine_vault_level_runtime_export\03_runtime_data_package\*" -Destination "I:\Disertation\VisualOptimizationUE\Content\VisualOptimization\RuntimeData" -Recurse -Force
```

Current UE compatibility path reads `materials/textures/<material_slot_id>/basecolor.png`.
The exporter also packages `materials/backend_candidates/` for future backend switching, but current UE code may ignore it until extended.
