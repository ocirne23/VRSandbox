export module UI.OutputLog;

import Core;
import Core.Log;

export class OutputLog
{
public:
	void render();

private:
	bool   m_showVerbose  = true;
	bool   m_showInfo     = true;
	bool   m_showWarning  = true;
	bool   m_showError    = true;
	bool   m_autoScroll   = true;
	char   m_filterBuf[256] = {};

	uint32                   m_cachedRevision = UINT32_MAX;
	std::vector<Log::Message> m_snapshot;
};
