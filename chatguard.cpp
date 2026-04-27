#include <stdio.h>
#include <sstream>
#include <regex>
#include "chatguard.h"
#include "metamod_oslink.h"
#include "schemasystem/schemasystem.h"

Chatguard g_chatguard;
PLUGIN_EXPOSE(Chatguard, g_chatguard);
IVEngineServer2* engine = nullptr;
CGameEntitySystem* g_pGameEntitySystem = nullptr;
CEntitySystem* g_pEntitySystem = nullptr;
CGlobalVars *gpGlobals = nullptr;

IUtilsApi* g_pUtils;
IPlayersApi* g_pPlayersApi;

std::map<std::string, std::string> g_vecPhrases;
std::vector<std::string> white_list;
std::vector<std::string> white_list_domain;

bool enableGuardIp;
bool enableGuardDomain;
std::string regular_ip;
std::string regular_domain;
std::string whiteip;
std::string white_domain;
bool writelog;
int punish_type;
std::string punish_cmd;

std::string ToLowerCase (std::string& slovo)
{
	std::string newStr = "";
	for (int i = 0; i < slovo.size(); i++)
	{
		newStr += tolower(slovo[i]);
	}
	return newStr;
}

std::vector<std::string> split (std::string msg, char delimiter) 
{
	std::vector<std::string> back;
	std::string stroka;

	std::istringstream strokastream(msg);

	while (std::getline(strokastream, stroka, delimiter)){
		back.push_back(stroka);
	}

	return back;
}

void ReplaceSteam (std::string& command, std::string replacement, std::string swap) 
{
	std::vector<std::string> str_replace = split(command, ' ');

	for (int i = 0; i < str_replace.size(); i++)
	{
		if (str_replace[i] == replacement)
		{
			str_replace[i] = swap;
		}
	}
	command = "";
	for (int i = 0; i < str_replace.size(); i++)
	{
		command += str_replace[i] + ' ';
	}
}

bool isIP (std::string msg)
{
	std::vector<std::string> message = split(msg, ' ');
	bool haveAnotherIp = false;

	for (int i = 0; i < message.size(); i++)
	{
		std::string checkIp = message[i];
		std::cmatch findIp;

		if (std::regex_search(checkIp.c_str(), findIp, std::regex(regular_ip)))
		{
			int countWhiteIp = 0;
			for (int j = 0; j < white_list.size(); j++)
			{
				if (white_list[j] == findIp[0])
				{
					countWhiteIp++;
				}
			}
			if (countWhiteIp == 0)
			{
				haveAnotherIp = true;
			}
		}	
	}

	if (haveAnotherIp) return true;
	else return false;
}

bool isDomain (std::string msg)
{
	std::vector<std::string> message = split(msg, ' ');
	bool haveAnotherDomain = false;

	for (int i = 0; i < message.size(); i++)
	{
		std::string checkDomain = message[i];
		std::cmatch findDomain;

		if (std::regex_search(checkDomain.c_str(), findDomain, std::regex(regular_domain)))
		{
			int countWhiteDomain = 0;
			std::string str_firstdomain = findDomain[0];
			std::string str_domain = ToLowerCase(str_firstdomain);
			for (int j = 0; j < white_list_domain.size(); j++)
			{
				std::string str_whiteDomain = ToLowerCase(white_list_domain[j]);
				if (str_whiteDomain == str_domain)
				{
					countWhiteDomain++;
				}
			}
			if (countWhiteDomain == 0)
			{
				haveAnotherDomain = true;
			}
		}	
	}

	if (haveAnotherDomain) return true;
	else return false;
}

CGameEntitySystem* GameEntitySystem()
{
	return g_pUtils->GetCGameEntitySystem();
}

void StartupServer()
{
	g_pGameEntitySystem = GameEntitySystem();
	g_pEntitySystem = g_pUtils->GetCEntitySystem();
	gpGlobals = g_pUtils->GetCGlobalVars();
}

bool Chatguard::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();

	GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetEngineFactory, g_pSchemaSystem, ISchemaSystem, SCHEMASYSTEM_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetFileSystemFactory, g_pFullFileSystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, engine, IVEngineServer2, SOURCE2ENGINETOSERVER_INTERFACE_VERSION);

	g_SMAPI->AddListener( this, this );

	return true;
}

bool Chatguard::Unload(char *error, size_t maxlen)
{
	ConVar_Unregister();
	
	return true;
}

void LoadConfig ()
{
	KeyValues* config = new KeyValues("Config");
	char pszPath[256];
	g_SMAPI->Format(pszPath, sizeof(pszPath), "addons/configs/ChatGuard/ChatGuard.ini");

	if (!config->LoadFromFile(g_pFullFileSystem, "addons/configs/ChatGuard/ChatGuard.ini")) {
		g_pUtils->ErrorLog("[%s] Failed to load config addons/configs/ChatGuard/ChatGuard.ini", g_PLAPI->GetLogTag());
		return;
	}

	enableGuardIp = config->GetBool("EnableIP", true);
	enableGuardDomain = config->GetBool("EnableDomain", true);
	regular_ip = config->GetString("RegularIP", "");
	regular_domain = config->GetString("RegularDomain", "");
	whiteip = config->GetString("WhiteListIP", "");
	white_domain = config->GetString("WhiteListDomain", "");
	writelog = config->GetBool("Logs", true);
	punish_type = config->GetInt("PunishType", 0);
	punish_cmd = config->GetString("CustomCommand", "");

	white_list = split(whiteip, ' ');
	white_list_domain = split(white_domain, ' ');
}

void LoadPhrases ()
{
	KeyValues::AutoDelete g_kvPhrases("Phrases");
	char pszPath[256];
	g_SMAPI->Format(pszPath, sizeof(pszPath), "addons/translations/chatguard.phrases.txt");

	if (!g_kvPhrases->LoadFromFile(g_pFullFileSystem, pszPath))
	{
		Warning("Failed to load %s\n", pszPath);
		return;
	}
	const char* szLanguage = g_pUtils->GetLanguage();
	for (KeyValues *pKey = g_kvPhrases->GetFirstTrueSubKey(); pKey; pKey = pKey->GetNextTrueSubKey())
		g_vecPhrases[std::string(pKey->GetName())] = std::string(pKey->GetString(szLanguage));
}

bool CheckIpInMessage (int iSlot, const char* szContent, bool bTeam)
{
	CCSPlayerController* player = CCSPlayerController::FromSlot(iSlot);
	if (!player) return false;

	if (enableGuardIp)
	{
		if (isIP(szContent))
		{
			uint64 steamid = g_pPlayersApi->GetSteamID64(iSlot);
			std::string s_steamid64 = std::to_string(steamid);

			switch (punish_type)
			{
				case 1:
				{
					g_pUtils->PrintToChat(iSlot, "%s %s", g_vecPhrases["Prefix"].c_str(), g_vecPhrases["HaveIP"].c_str());
					break;
				}
				case 2:
				{
					std::string msgkick = g_vecPhrases["Prefix"] + " " + g_vecPhrases["KickedUser"];
					g_pUtils->PrintToChatAll(msgkick.c_str(), player->m_iszPlayerName());
					engine->DisconnectClient(iSlot, NETWORK_DISCONNECT_KICKED);
					break;
				}
				case 3:
				{
					std::string copy_cmd = punish_cmd;
					ReplaceSteam(copy_cmd, "{steamid64}", s_steamid64);
					engine->ServerCommand(copy_cmd.c_str());
					break;
				}
				default:
				{
					break;
				}
			}
			if (writelog)
			{
				g_pUtils->LogToFile("ChatGuard", "Похоже %s (SteamID64: %s), написал какой-то IP адрес. Его сообщение: %s", player->m_iszPlayerName(), s_steamid64.c_str(), szContent);
			}
			return false;
		}
	}
	if (enableGuardDomain)
	{
		if (isDomain(szContent))
		{
			uint64 steamid = g_pPlayersApi->GetSteamID64(iSlot);
			std::string s_steamid64 = std::to_string(steamid);

			switch (punish_type)
			{
				case 1:
				{
					g_pUtils->PrintToChat(iSlot, "%s %s", g_vecPhrases["Prefix"].c_str(), g_vecPhrases["HaveDomain"].c_str());
					break;
				}
				case 2:
				{
					std::string msgkick = g_vecPhrases["Prefix"] + " " + g_vecPhrases["KickedUser"];
					g_pUtils->PrintToChatAll(msgkick.c_str(), player->m_iszPlayerName());
					engine->DisconnectClient(iSlot, NETWORK_DISCONNECT_KICKED);
					break;
				}
				case 3:
				{
					std::string copy_cmd = punish_cmd;
					ReplaceSteam(copy_cmd, "{steamid64}", s_steamid64);
					engine->ServerCommand(copy_cmd.c_str());
					break;
				}
				default:
				{
					break;
				}
			}
			if (writelog)
			{
				g_pUtils->LogToFile("ChatGuard", "Похоже %s (SteamID64: %s), написал какой-то домен. Его сообщение: %s", player->m_iszPlayerName(), s_steamid64.c_str(), szContent);
			}
			return false;
		}
	}
	return true;
}

void Chatguard::AllPluginsLoaded()
{
	char error[64];
	int ret;
	g_pUtils = (IUtilsApi *)g_SMAPI->MetaFactory(Utils_INTERFACE, &ret, NULL);
	if (ret == META_IFACE_FAILED)
	{
		g_SMAPI->Format(error, sizeof(error), "Missing Utils system plugin");
		ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
		std::string sBuffer = "meta unload "+std::to_string(g_PLID);
		engine->ServerCommand(sBuffer.c_str());
		return;
	}
	g_pPlayersApi = (IPlayersApi *)g_SMAPI->MetaFactory(PLAYERS_INTERFACE, &ret, NULL);
	if (ret == META_IFACE_FAILED)
	{
		g_SMAPI->Format(error, sizeof(error), "Missing Utils system plugin");
		ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
		std::string sBuffer = "meta unload "+std::to_string(g_PLID);
		engine->ServerCommand(sBuffer.c_str());
		return;
	}

	LoadPhrases();
	LoadConfig();

	g_pUtils->StartupServer(g_PLID, StartupServer);
	g_pUtils->AddChatListenerPre(g_PLID, CheckIpInMessage);
}

///////////////////////////////////////
const char* Chatguard::GetLicense()
{
	return "GPL";
}

const char* Chatguard::GetVersion()
{
	return "1.1";
}

const char* Chatguard::GetDate()
{
	return __DATE__;
}

const char *Chatguard::GetLogTag()
{
	return "ChatGuard";
}

const char* Chatguard::GetAuthor()
{
	return "Phoenix";
}

const char* Chatguard::GetDescription()
{
	return "Блокирует IP адрес в чате";
}

const char* Chatguard::GetName()
{
	return "ChatGuard";
}

const char* Chatguard::GetURL()
{
	return "https://discord.gg/2uUywgHG6E";
}
