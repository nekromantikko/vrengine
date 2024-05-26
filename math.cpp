#include "math.h"

// Thank you chatGPT
glm::quat AverageQuaternionsLogarithm(const glm::quat& q1, const glm::quat& q2) {
	// Compute the dot product
	float dot = glm::dot(q1, q2);
	glm::quat q2_adj = q2;

	// If the dot product is negative, negate one quaternion
	if (dot < 0.0f) {
		q2_adj = -q2;
	}

	// Compute the logarithms of the quaternions
	glm::vec3 log_q1 = glm::angle(q1) * glm::axis(q1);
	glm::vec3 log_q2 = glm::angle(q2_adj) * glm::axis(q2_adj);

	// Average the logarithms
	glm::vec3 log_avg = (log_q1 + log_q2) / 2.0f;
	float magnitude = glm::length(log_avg);

	// Handle zero magnitude case
	if (magnitude > 0.0f) {
		glm::vec3 axis = log_avg / magnitude;
		return glm::normalize(glm::angleAxis(magnitude, axis));
	} else {
		return glm::quat(1, 0, 0, 0); // Identity quaternion
	}
}