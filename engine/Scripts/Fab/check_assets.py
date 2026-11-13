import unreal

assets = unreal.EditorAssetLibrary.list_assets('/Game/PK_Equestrian', recursive=True, include_folder=False)
failed = []
for path in assets:
    try:
        obj = unreal.EditorAssetLibrary.load_asset(path)
        if obj is None:
            failed.append(path)
    except Exception as exc:  # noqa: BLE001
        failed.append(path)
        unreal.log_error('FAB_LOAD_EXCEPTION {} {}'.format(path, exc))

unreal.log('FAB_LOAD_CHECK total={} failed={}'.format(len(assets), len(failed)))
for path in failed[:50]:
    unreal.log_error('FAB_LOAD_FAIL {}'.format(path))

unreal.SystemLibrary.quit_editor()
