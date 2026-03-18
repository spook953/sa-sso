#include "SSO/Manager.hpp"

class Mod final
{
private:
    SSO::Manager sso_mgr{};

public:
    Mod()
    {
        plugin::Events::drawHudEvent.before += [&]()
        {
            sso_mgr.Initialize(SSO::Style::BLUR);

            for (CPed *const ped : CPools::ms_pPedPool)
            {
                if (!ped || !ped->IsAlive() || !ped->IsVisible()) {
                    continue;
                }
                
                sso_mgr.AddEntity(ped, { 255, 255, 255, 255 });
            }

            for (CVehicle *const veh : CPools::ms_pVehiclePool)
            {
                if (!veh || veh->m_fHealth <= 0.0f || !veh->IsVisible()) {
                    continue;
                }
                
                sso_mgr.AddEntity(veh, { 255, 0, 255, 255 });
            }

            sso_mgr.Render();
        };

        plugin::Events::d3dLostEvent += [&]()
        {
            sso_mgr.Shutdown();
        };
    }
} mod;