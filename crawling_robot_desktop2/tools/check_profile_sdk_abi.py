"""Source-contract checks against installed SDK; no Qt compilation/device IO."""
from pathlib import Path
import re
import struct

root = Path(__file__).resolve().parents[1]
sdk = Path('D:/3DMVS/Development/Includes/Mv3dLpDefine.h').read_text(encoding='utf-8-sig')
wrapper = (root.parent/'modules/mv3dlp_laser_profile/windows_x64/src/vendor_sdk.hpp').read_text(encoding='utf-8')
driver = (root.parent/'modules/mv3dlp_laser_profile/windows_x64/src/driver.cpp').read_text(encoding='utf-8')
official = re.search(r'MV3D_LP_ProfileDataCallBack\)\s*\(([^;]+)\);', sdk).group(1)
local = re.search(r'using ProfileDataCallback\s*=.*?\((ProfileDataRaw\*.*?)\);', wrapper, re.S).group(1)
assert len(official.split(',')) == len(local.split(',')) == 3
assert re.search(r'profileCallbackThunk\(vendor::ProfileDataRaw\* raw,\s*vendor::IntensityDataRaw\*, void\* user\)', driver)
assert 'int16_t nX;' in sdk and 'std::int16_t coordinates[3]' in driver
assert 'std::numeric_limits<std::uint16_t>::max()' not in driver
# Signed negative X must survive packed decoding rather than wrap to +47008.
packed = struct.pack('<hhh', -18528, 0, 3252)
assert struct.unpack('<hhh',packed) == (-18528,0,3252)
print('Installed SDK contract: three-argument callback and signed XYZ16 verified (source only).')
