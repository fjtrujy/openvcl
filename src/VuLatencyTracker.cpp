#include "VuLatencyTracker.h"

#include "Operand.h"
#include "VuSchedulingRules.h"

#include <list>

namespace vcl
{

VuLatencyTracker::VuLatencyTracker()
{
	reset();
}

void VuLatencyTracker::reset()
{
	m_qReadyCycle = -10;
	m_pReadyCycle = -10;
	m_lastFMACCycle = -10;
	m_lastClipwCycle = -10;
	m_registerReadyCycle.clear();
	m_registerProducerMnemonic.clear();
}

int VuLatencyTracker::readHazardDelay( const Token& token,
                                       const Token* partner,
                                       int currentCycle ) const
{
	std::list<std::string> reads;
	collectVuRegisterReadKeys( token, reads );
	if( partner )
		collectVuRegisterReadKeys( *partner, reads );

	bool readsQ = vuTokenReadsQ( token );
	bool readsP = vuTokenReadsP( token );
	if( partner )
	{
		readsQ = readsQ || vuTokenReadsQ( *partner );
		readsP = readsP || vuTokenReadsP( *partner );
	}

	int needed = 0;
	for( std::list<std::string>::const_iterator i = reads.begin(); i != reads.end(); ++i )
	{
		std::map<std::string, int>::const_iterator ready = m_registerReadyCycle.find( *i );
		if( ready == m_registerReadyCycle.end() )
			continue;

		int readyCycle = ready->second;
		std::map<std::string, std::string>::const_iterator producer =
		    m_registerProducerMnemonic.find( *i );
		if( producer != m_registerProducerMnemonic.end()
		    && isVuFtoiConversion( producer->second )
		    && ( (isVuMtir( token ) && vuTokenReadsRegister( token, *i ))
		         || (partner && isVuMtir( *partner ) && vuTokenReadsRegister( *partner, *i )) ) )
			readyCycle -= 4;
		if( producer != m_registerProducerMnemonic.end()
		    && isVuLoadToFtoiBypassProducer( producer->second )
		    && ( (isVuFtoiConversion( lowerVuTokenName( token ) ) && vuTokenReadsRegister( token, *i ))
		         || (partner && isVuFtoiConversion( lowerVuTokenName( *partner ) )
		             && vuTokenReadsRegister( *partner, *i )) ) )
			readyCycle -= 4;

		const int gap = readyCycle - currentCycle;
		if( gap > needed )
			needed = gap;
	}

	if( readsQ )
	{
		const int gap = m_qReadyCycle - currentCycle;
		if( gap > needed )
			needed = gap;
	}
	if( readsP )
	{
		const int gap = m_pReadyCycle - currentCycle;
		if( gap > needed )
			needed = gap;
	}

	const int flagCycle = currentCycle + needed;
	bool readsMac = token.operand() && isVuMacReader( token.operand()->name() );
	bool readsClip = token.operand() && isVuClipReader( token.operand()->name() );
	if( partner && partner->operand() )
	{
		readsMac = readsMac || isVuMacReader( partner->operand()->name() );
		readsClip = readsClip || isVuClipReader( partner->operand()->name() );
	}

	int flagDelay = 0;
	if( readsMac )
	{
		const int gap = flagCycle - m_lastFMACCycle;
		if( 4 - gap > flagDelay )
			flagDelay = 4 - gap;
	}
	if( readsClip )
	{
		const int gap = flagCycle - m_lastClipwCycle;
		if( 4 - gap > flagDelay )
			flagDelay = 4 - gap;
	}
	if( flagDelay > 0 )
		needed += flagDelay;

	return needed;
}

void VuLatencyTracker::recordWrites( const Token& token, int issueCycle, bool forceMacFlagWrite )
{
	if( forceMacFlagWrite || (token.operand() && token.operand()->unit() == Operand::FMAC) )
		m_lastFMACCycle = issueCycle;
	if( token.operand() && isVuClipw( token.operand()->name() ) )
		m_lastClipwCycle = issueCycle;

	if( !token.operand() || token.operand()->latency() <= 1 )
		return;

	const int readyCycle = issueCycle + static_cast<int>( token.operand()->latency() ) + 1;
	std::list<std::string> writes;
	collectVuRegisterWriteKeys( token, writes );
	for( std::list<std::string>::const_iterator i = writes.begin(); i != writes.end(); ++i )
	{
		m_registerReadyCycle[*i] = readyCycle;
		m_registerProducerMnemonic[*i] = lowerVuTokenName( token );
	}

	if( vuTokenWritesQ( token ) )
		m_qReadyCycle = readyCycle;
	if( vuTokenWritesP( token ) )
		m_pReadyCycle = readyCycle;
}

int VuLatencyTracker::qReadyCycle() const
{
	return m_qReadyCycle;
}

int VuLatencyTracker::pReadyCycle() const
{
	return m_pReadyCycle;
}

}
