#ifndef __OPENVCL_VULATENCYTRACKER_H__
#define __OPENVCL_VULATENCYTRACKER_H__

/*
 * VuLatencyTracker.h
 *
 * Shared VU readiness model used by code emission and scheduler analysis.
 */

#include "Token.h"

#include <map>
#include <string>

namespace vcl
{

class VuLatencyTracker
{
public:
	VuLatencyTracker();

	void reset();
	int readHazardDelay( const Token& token, const Token* partner, int currentCycle ) const;
	void recordWrites( const Token& token, int issueCycle, bool forceMacFlagWrite = false );

	int qReadyCycle() const;
	int pReadyCycle() const;

private:
	int m_qReadyCycle;
	int m_pReadyCycle;
	int m_lastFMACCycle;
	int m_lastClipwCycle;
	std::map<std::string, int> m_registerReadyCycle;
	std::map<std::string, std::string> m_registerProducerMnemonic;
};

}

#endif
