#include <iostream>

int main() {
  std::cerr << "legacy_target_old_client_deposit_sequence_pending: PR-1 records this as "
               "unconfirmed; do not blindly assert SM_DELITEM -> SM_STORAGE_OK -> "
               "SM_WEIGHTCHANGED as Delphi behavior.\n";
  return 0;
}
