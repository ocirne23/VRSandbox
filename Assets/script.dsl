//@@dsl 1
//@function update(float deltaSec)
//@	
//@end
//@
//@function countPrimes(int start, int end) -> int
//@	int numPrimes = 0
//@	for int i = start, i < end, i += 1
//@		if isPrime(int n = i)
//@			numPrimes += 1
//@		end
//@	end
//@	return numPrimes
//@end
//@
//@function isPrime(int n) -> bool
//@	if n < 2
//@		return false
//@	end
//@	for int i = 0, i < (n / 2), i += 1
//@		if (i % n) == 0
//@			return false
//@		end
//@	end
//@	return true
//@end
//@@end
