inline Alias::Alias( Type type ) : m_type(type), m_allocatedRegister(NULL), m_sameNamePredecessor(NULL)
{
}

inline void Alias::setSameNamePredecessor( Alias* predecessor )
{
	m_sameNamePredecessor = predecessor;
}

inline Alias* Alias::sameNamePredecessor() const
{
	return m_sameNamePredecessor;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline void Alias::setAllocatedRegister( const Register* allocated )
{
	m_allocatedRegister = allocated;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline const Register* Alias::allocatedRegister() const
{
	return m_allocatedRegister;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline Alias::Type Alias::type() const
{
	return m_type;
}
